/*
  EDA-triggered atomizer, stage 1 (no SD card yet).
  Board: Arduino Uno R3 (5 V logic, 10-bit ADC).

  WIRING
    EDA front end (Poh et al. 2010 Fig. 10a, built on 3.3 V):
      Uno 3V3  -> circuit +3.3 V rail AND the Uno AREF pin
      Uno GND  -> circuit GND rail
      op-amp Vo (pin 1)                         -> A0
      Vcc/2 node (divider middle, op-amp pin 5) -> A1   (only if USE_VREF_CHANNEL is 1)
      Electrode A -> +3.3 V rail, Electrode B -> sense node (op-amp pin 2)
    Atomizer module:
      module V -> Uno 5V, module G -> Uno GND, module S -> D7
    Pins D10-D13 are left free for the SD card in stage 2 (CS, MOSI, MISO, SCK).

  WHY AREF = 3V3
    The front end runs from 3.3 V, so Vo never exceeds ~1.65 V. With the ADC reference at 3.3 V
    instead of 5 V the readings use about 50% more of the range. Because the ADC reference and
    the circuit supply are the same net, supply drift cancels in the conductance calculation.
    analogReference(EXTERNAL) is set first in setup(). Never put a voltage on AREF while the
    default reference is selected.

  CONDUCTANCE (derived from the circuit at DC with ideal op-amps; not stated in the paper)
    With ref = the Vcc/2 node and all voltages in ADC counts (Vcc = ADC_FULL counts):
      G * R1 = 2 (ref - Vo) / (Vo + Vcc - 2 ref)
    which reduces to G = (Vcc/Vo - 2) / R1 when ref = Vcc/2 exactly.

  TRIGGER LOGIC
    gFast   = G smoothed over about 1 s
    gBase   = slow baseline (time constant BASELINE_TAU_S), set from a calibration window
    phasic  = gFast - gBase
    When phasic stays >= TRIGGER_RISE_US for CONFIRM_MS the atomizer runs for BURST_MS, then a
    COOLDOWN_MS lockout follows (and, by default, phasic must settle below half the trigger
    before the next burst, so one excursion gives one burst). Losing electrode contact forces everything off and restarts the
    calibration, so putting the electrodes on never counts as a response.
    The defaults below are placeholders. Tune them against your own recordings.

  OUTPUT (115200 baud, CSV, lines starting with # are comments)
    ms,vo_counts,g_raw_uS,g_fast_uS,g_base_uS,phasic_uS,atomizer,state,status
    state : 0 no contact, 1 calibrating, 2 armed, 3 misting, 4 cooldown
    status: 0 ok, 1 Vo too low (conductance out of range), 2 no contact

  SERIAL COMMANDS
    a = automatic (EDA-triggered)   o = force on   f = force off
    t = test burst (BURST_MS)       c = recalibrate   ? = help
*/

#include <math.h>

// ---------------- Pins and build options ----------------
const uint8_t PIN_VO       = A0;
const uint8_t PIN_VREF     = A1;
const uint8_t PIN_ATOMIZER = 7;
#define USE_VREF_CHANNEL 0   // 1: also measure the Vcc/2 node on A1 (corrects divider mismatch)
#define MIRROR_LED       1   // onboard LED mirrors the atomizer. Set to 0 when adding the SD card (D13 is SCK)

// ---------------- EDA front end ----------------
const float R1_OHMS       = 1.0e6f;  // measure your real R1 and enter it here
const float CAL_GAIN      = 1.0f;    // G_reported = CAL_GAIN * G + CAL_OFFSET_US (fit from known resistors)
const float CAL_OFFSET_US = 0.0f;
const uint16_t SAMPLE_HZ  = 32;
const uint8_t  OVERSAMPLE = 16;
const float FAST_ALPHA    = 0.3f;    // about 1 s smoothing at 32 Hz
const float NO_CONTACT_US = 0.05f;   // below this the electrodes are treated as off the skin

// ---------------- Trigger ----------------
const unsigned long CONTACT_SETTLE_MS = 2000;
const float         CALIBRATION_S     = 30.0f;   // dry electrodes keep settling for minutes, so use longer for real runs
const float         BASELINE_TAU_S    = 60.0f;
const float         TRIGGER_RISE_US   = 0.3f;
const unsigned long CONFIRM_MS        = 500;
const unsigned long BURST_MS          = 5000;
const unsigned long COOLDOWN_MS       = 30000;
const unsigned long MAX_CONTINUOUS_ON_MS = 15000;   // hard safety cutoff, any mode
const bool REARM_NEEDS_RELAX = true;   // after cooldown, wait until phasic falls below half the trigger before re-arming,
                                       // so one sustained excursion gives one burst. Set false for repeat bursts while aroused.

// ---------------- Atomizer ----------------
const bool ACTIVE_HIGH = true;   // module S: HIGH = spray (manufacturer's example)

// ---------------- ADC ----------------
#if defined(ARDUINO_ARCH_AVR)
  const uint8_t ADC_BITS = 10;
#else
  const uint8_t ADC_BITS = 12;
#endif
const float ADC_FULL = (float)(1UL << ADC_BITS);
const float LOW_COUNT_LIMIT = 8.0f * ADC_FULL / 1024.0f;
const uint8_t PRINT_EVERY_N = 4;   // 32 Hz sampling, 8 Hz printing

// ---------------- State ----------------
const uint8_t ST_NO_CONTACT = 0, ST_CALIBRATING = 1, ST_ARMED = 2, ST_MIST = 3, ST_COOLDOWN = 4;
const uint8_t OV_NONE = 0, OV_ON = 1, OV_OFF = 2;

uint8_t state = ST_NO_CONTACT;
uint8_t ovr = OV_NONE;
unsigned long ovrUntil = 0;
unsigned long stateStart = 0, contactSince = 0, aboveSince = 0, onSince = 0;
bool contactTimer = false, aboveTimer = false, atomizerOn = false, needRelax = false;
float gFast = NAN, gBase = NAN, calSum = 0, alphaBase = 0;
uint32_t calN = 0;
uint8_t printCount = 0;
uint32_t nextTickUs;

static float readAveraged(uint8_t pin) {
  (void)analogRead(pin);   // throwaway read after a mux change
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
  gBase = NAN;
}

static void sampleStep(unsigned long now) {
  float cVo = readAveraged(PIN_VO);
#if USE_VREF_CHANNEL
  float cRef = readAveraged(PIN_VREF);
#else
  float cRef = ADC_FULL / 2.0f;
#endif

  uint8_t status = 0;
  float g = NAN;
  float den = cVo + ADC_FULL - 2.0f * cRef;
  if (cVo < LOW_COUNT_LIMIT || den <= 0.0f) {
    status = 1;
  } else {
    g = 1.0e6f * 2.0f * (cRef - cVo) / (R1_OHMS * den);
    g = CAL_GAIN * g + CAL_OFFSET_US;
    if (g < 0.0f) g = 0.0f;
    if (g < NO_CONTACT_US) status = 2;
  }
  bool contact = (status != 2);

  if (!contact) {
    gFast = NAN;
    gBase = NAN;
    state = ST_NO_CONTACT;
    contactTimer = false;
    aboveTimer = false;
  } else {
    if (state == ST_NO_CONTACT) {
      if (!contactTimer) { contactTimer = true; contactSince = now; }
      if (now - contactSince >= CONTACT_SETTLE_MS) startCalibration(now);
    }
    if (status == 0) {
      gFast = isnan(gFast) ? g : gFast + FAST_ALPHA * (g - gFast);
      if (state == ST_CALIBRATING) {
        calSum += gFast;
        calN++;
      } else if (state != ST_NO_CONTACT) {
        gBase += alphaBase * (gFast - gBase);
        if (state == ST_ARMED) {
          if (needRelax && (gFast - gBase) < TRIGGER_RISE_US * 0.5f) needRelax = false;
          if (needRelax) {
            aboveTimer = false;   // still elevated from the last response: wait for it to settle
          } else if (gFast - gBase >= TRIGGER_RISE_US) {
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
    }
    if (state == ST_CALIBRATING && calN > 0 && (now - stateStart) >= (unsigned long)(CALIBRATION_S * 1000.0f)) {
      gBase = calSum / (float)calN;
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
    float phasic = gFast - gBase;
    Serial.print(now);                 Serial.print(',');
    Serial.print(cVo, 1);              Serial.print(',');
    Serial.print(g, 3);                Serial.print(',');
    Serial.print(gFast, 3);            Serial.print(',');
    Serial.print(gBase, 3);            Serial.print(',');
    Serial.print(phasic, 3);           Serial.print(',');
    Serial.print((int)atomizerOn);     Serial.print(',');
    Serial.print((int)state);          Serial.print(',');
    Serial.println((int)status);
  }
}

void setup() {
#if defined(ARDUINO_ARCH_AVR)
  analogReference(EXTERNAL);   // AREF is wired to 3V3. Must come before any analogRead.
#else
  analogReadResolution(ADC_BITS);
#endif
  pinMode(PIN_ATOMIZER, OUTPUT);
#if MIRROR_LED
  pinMode(LED_BUILTIN, OUTPUT);
#endif
  setAtomizer(false, 0);       // always start off
  alphaBase = 1.0f - expf(-1.0f / ((float)SAMPLE_HZ * BASELINE_TAU_S));

  Serial.begin(115200);
  for (uint8_t i = 0; i < 5; i++) (void)analogRead(PIN_VO);   // let the ADC settle on the new reference
  Serial.println(F("# EDA-triggered atomizer, stage 1"));
  printHelp();
  Serial.println(F("ms,vo_counts,g_raw_uS,g_fast_uS,g_base_uS,phasic_uS,atomizer,state,status"));
  nextTickUs = micros();
}

void loop() {
  unsigned long nowMs = millis();
  handleSerial(nowMs);

  if ((int32_t)(micros() - nextTickUs) < 0) return;
  nextTickUs += 1000000UL / SAMPLE_HZ;
  sampleStep(nowMs);
}
