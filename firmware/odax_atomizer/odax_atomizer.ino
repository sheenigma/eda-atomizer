/*
  ODAX-EDA-triggered atomizer, stage 1 (no SD card yet).
  Board: Arduino Uno R3 (5 V logic, 10-bit ADC, default analog reference).

  WIRING
    ODAX board terminal block:  VCC -> Uno 5V,  GND -> Uno GND,  OUT -> A0
      (electrode leads plug into the board's 3.5 mm jack, straps on two fingers of one hand)
    Atomizer module:            V -> Uno 5V,  G -> Uno GND,  S -> D7
    Pins D10-D13 stay free for the SD card in stage 2 (CS, MOSI, MISO, SCK).

  WHAT THIS SKETCH DOES NOT KNOW
    ODAX's public pages do not state how OUT relates to microsiemens, or whether OUT rises or falls
    when conductance rises. So this sketch works on the raw ADC value and detects a change relative
    to a slow baseline:
        sig    = POLARITY * counts
        phasic = sig_fast - sig_base
    Run odax_probe.ino first to find POLARITY, a sensible TRIGGER_RISE_COUNTS and the no-contact level.
    If you get ODAX's datasheet transfer function, microsiemens can be added.

  TRIGGER LOGIC
    When phasic stays >= TRIGGER_RISE_COUNTS for CONFIRM_MS the atomizer runs for BURST_MS, then a
    COOLDOWN_MS lockout follows, and by default phasic must settle below half the trigger before the
    next burst (one excursion = one burst). If a no-contact level is configured, taking the
    electrodes off forces the atomizer off and restarts calibration, so putting them on never
    counts as a response. All numbers below are placeholders to tune against your own recordings.

  OUTPUT (115200 baud, CSV, lines starting with # are comments)
    ms,counts,mv_est,sig_fast,sig_base,phasic,atomizer,state,status
    state : 0 no contact/settling, 1 calibrating, 2 armed, 3 misting, 4 cooldown
    status: 0 ok, 1 no contact

  SERIAL COMMANDS
    a = automatic   o = force on   f = force off
    t = test burst (BURST_MS)      c = recalibrate   ? = help
*/

#include <math.h>

// ---------------- Pins and build options ----------------
const uint8_t PIN_SIGNAL   = A0;
const uint8_t PIN_ATOMIZER = 7;
#define MIRROR_LED 1   // onboard LED mirrors the atomizer. Set to 0 when adding the SD card (D13 is SCK)

// ---------------- ODAX signal ----------------
const float POLARITY = 1.0f;          // +1 if OUT rises with arousal, -1 if it falls. Measured: contact raises counts, so +1
const int   NO_CONTACT_BELOW = 1;     // counts below this = electrodes off. Measured: open air reads 0.0, skin 3-17. -1 disables
const int   NO_CONTACT_ABOVE = 1024;  // counts above this = electrodes off. 1024 disables
const float VREF_MV = 5000.0f;        // only used for the mv_est column. Measure your 5V pin
const uint16_t SAMPLE_HZ = 32;
const uint8_t  OVERSAMPLE = 16;
const float FAST_TAU_S = 1.0f;        // smoothing time constant of the fast signal

// ---------------- Trigger ----------------
const unsigned long CONTACT_SETTLE_MS = 2000;
const float         CALIBRATION_S     = 60.0f;   // settling wait after contact. Measured: counts keep rising ~40 s after contact
const float         BASELINE_TAU_S    = 60.0f;
const float         TRIGGER_RISE_COUNTS = 4.0f;  // provisional, about 20 mV at 5 V. From one clean response (+6) and ~2 counts of rest wiggle. Re-tune with more data
const unsigned long CONFIRM_MS        = 500;
const unsigned long BURST_MS          = 5000;
const unsigned long COOLDOWN_MS       = 30000;
const unsigned long MAX_CONTINUOUS_ON_MS = 15000;   // hard safety cutoff, any mode
const bool REARM_NEEDS_RELAX = true;   // after cooldown, wait until phasic is below half the trigger before re-arming

// ---------------- Atomizer ----------------
const bool ACTIVE_HIGH = true;   // module S: HIGH = spray (manufacturer's example)

// ---------------- ADC ----------------
const float ADC_FULL = 1024.0f;
const uint8_t PRINT_EVERY_N = 4;   // 32 Hz sampling, 8 Hz printing

// ---------------- State ----------------
const uint8_t ST_NO_CONTACT = 0, ST_CALIBRATING = 1, ST_ARMED = 2, ST_MIST = 3, ST_COOLDOWN = 4;
const uint8_t OV_NONE = 0, OV_ON = 1, OV_OFF = 2;

uint8_t state = ST_NO_CONTACT;
uint8_t ovr = OV_NONE;
unsigned long ovrUntil = 0;
unsigned long stateStart = 0, contactSince = 0, aboveSince = 0, onSince = 0;
bool contactTimer = false, aboveTimer = false, atomizerOn = false, needRelax = false;
float sFast = NAN, sBase = NAN, calSum = 0, alphaBase = 0, alphaFast = 0;
uint32_t calN = 0;
uint8_t printCount = 0;
uint32_t nextTickUs;

static float readAveraged(uint8_t pin) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < OVERSAMPLE; i++) sum += analogRead(pin);
  return (float)sum / OVERSAMPLE;
}

static void setAtomizer(bool on, unsigned long now) {
  digitalWrite(PIN_ATOMIZER, (on == ACTIVE_HIGH) ? HIGH : LOW);
#if MIRROR_LED
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
#endif
  atomizerOn = on;
  if (on) onSince = now;
}

static void printHelp() {
  Serial.println(F("# commands: a auto, o force on, f force off, t test burst, c recalibrate, ? help"));
}

static void handleSerial(unsigned long now) {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'a')      { ovr = OV_NONE; Serial.println(F("# mode: auto")); }
    else if (c == 'o') { ovr = OV_ON;  ovrUntil = 0; Serial.println(F("# forced on")); }
    else if (c == 'f') { ovr = OV_OFF; ovrUntil = 0; Serial.println(F("# forced off")); }
    else if (c == 't') { ovr = OV_ON;  ovrUntil = now + BURST_MS; Serial.println(F("# test burst")); }
    else if (c == 'c') { state = ST_NO_CONTACT; contactTimer = false; Serial.println(F("# recalibrating")); }
    else if (c == '?') { printHelp(); }
  }
}

static void startCalibration(unsigned long now) {
  state = ST_CALIBRATING;
  stateStart = now;
  calSum = 0;
  calN = 0;
  sBase = NAN;
}

static void sampleStep(unsigned long now) {
  float counts = readAveraged(PIN_SIGNAL);
  float sig = POLARITY * counts;

  uint8_t status = 0;
  if (counts < (float)NO_CONTACT_BELOW || counts > (float)NO_CONTACT_ABOVE) status = 1;
  bool contact = (status == 0);

  if (!contact) {
    sFast = NAN;
    sBase = NAN;
    state = ST_NO_CONTACT;
    contactTimer = false;
    aboveTimer = false;
  } else {
    if (state == ST_NO_CONTACT) {
      if (!contactTimer) { contactTimer = true; contactSince = now; }
      if (now - contactSince >= CONTACT_SETTLE_MS) startCalibration(now);
    }
    sFast = isnan(sFast) ? sig : sFast + alphaFast * (sig - sFast);
    if (state == ST_CALIBRATING) {
      calSum += sFast;
      calN++;
    } else if (state != ST_NO_CONTACT) {
      sBase += alphaBase * (sFast - sBase);
      if (state == ST_ARMED) {
        if (needRelax && (sFast - sBase) < TRIGGER_RISE_COUNTS * 0.5f) needRelax = false;
        if (needRelax) {
          aboveTimer = false;   // still elevated from the last response: wait for it to settle
        } else if (sFast - sBase >= TRIGGER_RISE_COUNTS) {
          if (!aboveTimer) { aboveTimer = true; aboveSince = now; }
          if (now - aboveSince >= CONFIRM_MS) {
            state = ST_MIST;
            stateStart = now;
            aboveTimer = false;
          }
        } else {
          aboveTimer = false;
        }
      }
    }
    if (state == ST_CALIBRATING && calN > 0 && (now - stateStart) >= (unsigned long)(CALIBRATION_S * 1000.0f)) {
      sBase = sFast;   // start the baseline at the current level; the window mean is biased low while the signal is still settling
      state = ST_ARMED;
      aboveTimer = false;
      needRelax = false;
    }
    if (state == ST_MIST && (now - stateStart) >= BURST_MS) {
      state = ST_COOLDOWN;
      stateStart = now;
    }
    if (state == ST_COOLDOWN && (now - stateStart) >= COOLDOWN_MS) {
      state = ST_ARMED;
      aboveTimer = false;
      needRelax = REARM_NEEDS_RELAX;
    }
  }

  // ---- decide the atomizer output: manual override wins over the EDA trigger ----
  if (ovr != OV_NONE && ovrUntil != 0 && (long)(now - ovrUntil) >= 0) ovr = OV_NONE;
  bool want = (ovr == OV_NONE) ? (state == ST_MIST) : (ovr == OV_ON);
  if (want && atomizerOn && (now - onSince) >= MAX_CONTINUOUS_ON_MS) {
    want = false;
    ovr = OV_OFF;
    ovrUntil = 0;
    Serial.println(F("# safety cutoff: atomizer off. Send a to resume auto."));
  }
  if (want != atomizerOn) setAtomizer(want, now);

  if (++printCount >= PRINT_EVERY_N) {
    printCount = 0;
    float phasic = sFast - sBase;
    Serial.print(now);                         Serial.print(',');
    Serial.print(counts, 1);                   Serial.print(',');
    Serial.print(counts * VREF_MV / ADC_FULL, 0); Serial.print(',');
    Serial.print(sFast, 2);                    Serial.print(',');
    Serial.print(sBase, 2);                    Serial.print(',');
    Serial.print(phasic, 2);                   Serial.print(',');
    Serial.print((int)atomizerOn);             Serial.print(',');
    Serial.print((int)state);                  Serial.print(',');
    Serial.println((int)status);
  }
}

void setup() {
  pinMode(PIN_ATOMIZER, OUTPUT);
#if MIRROR_LED
  pinMode(LED_BUILTIN, OUTPUT);
#endif
  setAtomizer(false, 0);       // always start off
  alphaBase = 1.0f - expf(-1.0f / ((float)SAMPLE_HZ * BASELINE_TAU_S));
  alphaFast = 1.0f - expf(-1.0f / ((float)SAMPLE_HZ * FAST_TAU_S));

  Serial.begin(115200);
  (void)analogRead(PIN_SIGNAL);
  Serial.println(F("# ODAX-triggered atomizer, stage 1"));
  printHelp();
  Serial.println(F("ms,counts,mv_est,sig_fast,sig_base,phasic,atomizer,state,status"));
  nextTickUs = micros();
}

void loop() {
  unsigned long nowMs = millis();
  handleSerial(nowMs);

  if ((int32_t)(micros() - nextTickUs) < 0) return;
  nextTickUs += 1000000UL / SAMPLE_HZ;
  sampleStep(nowMs);
}
