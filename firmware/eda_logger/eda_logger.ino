/*
  EDA logger for the auto-bias front end in Poh, Swenson & Picard, IEEE TBME 2010 (Fig. 10a).

  Wiring (3.3 V board assumed):
    Vo  (op-amp pin 1)               -> PIN_VO
    Vcc/2 node (divider, pin 5)      -> PIN_VREF   (optional, see USE_VREF_CHANNEL)
    GND of the circuit               -> board GND
    3.3 V rail of the circuit        -> board 3V3

  Math (derived from the circuit at DC, ideal op-amps; not stated in the paper):
    Vo = Vcc / (2 + G*R1)   ->   G = (Vcc/Vo - 2) / R1
  With the Vcc/2 node measured on a second channel, Vcc/Vo = 2*ref/Vo, so
    G = 2*(ref/Vo - 1) / R1
  and the ADC reference voltage drops out of the calculation entirely.

  Output: CSV at SAMPLE_HZ over Serial at 115200 baud.
    ms, adc_vo, adc_ref, G_uS_raw, G_uS_filtered, status
  status: 0 = ok, 1 = Vo reading too low (conductance above the usable range)
*/

// ---------------- Pins ----------------
#define PIN_VO   A0
#define PIN_VREF A1          // Vcc/2 reference node
#define USE_VREF_CHANNEL 1   // 1: ratiometric using PIN_VREF (recommended). 0: assume ADC reference == Vcc.

// ---------------- Circuit constants ----------------
const float R1_OHMS = 1.0e6f;   // measure your actual R1 with a multimeter and enter it here

// ---------------- Sampling ----------------
const uint16_t SAMPLE_HZ  = 32;   // the paper used 32 Hz
const uint8_t  OVERSAMPLE = 16;   // readings averaged per sample, reduces ADC noise
const float    EMA_ALPHA  = 0.3f; // software smoothing, 1.0 = off. Hardware already low-passes at ~1.6 Hz.

// ---------------- Calibration (see notes) ----------------
// G_reported = CAL_GAIN * G_computed + CAL_OFFSET_US
const float CAL_GAIN      = 1.0f;
const float CAL_OFFSET_US = 0.0f;

// ---------------- ADC setup ----------------
#if defined(ARDUINO_ARCH_AVR)
  const uint8_t ADC_BITS = 10;   // classic Uno/Nano: 10-bit
#else
  const uint8_t ADC_BITS = 12;   // nRF52, SAMD, RP2040, ESP32, etc.
#endif
const uint16_t ADC_MAX = (1u << ADC_BITS) - 1;
const uint16_t LOW_COUNT_LIMIT = 8;   // Vo below this many counts is treated as out of range

static float readAveraged(uint8_t pin) {
  (void)analogRead(pin);              // throwaway read lets the ADC input settle after a mux change
  uint32_t sum = 0;
  for (uint8_t i = 0; i < OVERSAMPLE; i++) sum += analogRead(pin);
  return (float)sum / OVERSAMPLE;
}

static uint32_t nextTickUs;
static float    filtered = NAN;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}   // wait briefly for USB serial boards

#if defined(ARDUINO_ARCH_AVR)
  // On an Uno/Nano, feed 3.3 V to the AREF pin and use it as the reference
  // so the ADC range matches the 3.3 V circuit.
  analogReference(EXTERNAL);
#else
  analogReadResolution(ADC_BITS);
#endif

  Serial.println(F("ms,adc_vo,adc_ref,G_uS_raw,G_uS_filtered,status"));
  nextTickUs = micros();
}

void loop() {
  // fixed-rate timing, robust to micros() rollover
  if ((int32_t)(micros() - nextTickUs) < 0) return;
  nextTickUs += 1000000UL / SAMPLE_HZ;

  float cVo = readAveraged(PIN_VO);

#if USE_VREF_CHANNEL
  float cRef = readAveraged(PIN_VREF);
#else
  float cRef = ADC_MAX / 2.0f;   // assumes ADC full scale == Vcc, so the Vcc/2 node reads half scale
#endif

  uint8_t status = 0;
  float g_uS = NAN;

  if (cVo < LOW_COUNT_LIMIT) {
    status = 1;                  // Vo is essentially at ground: far above the usable range
  } else {
    float ratio = cRef / cVo;                         // = Vcc / (2 * Vo)
    float g_siemens = 2.0f * (ratio - 1.0f) / R1_OHMS;
    g_uS = g_siemens * 1.0e6f;
    g_uS = CAL_GAIN * g_uS + CAL_OFFSET_US;
    if (g_uS < 0) g_uS = 0;      // tiny negatives are noise or offset near open circuit
  }

  if (!isnan(g_uS)) {
    filtered = isnan(filtered) ? g_uS : (EMA_ALPHA * g_uS + (1.0f - EMA_ALPHA) * filtered);
  }

  Serial.print(millis());       Serial.print(',');
  Serial.print(cVo, 1);         Serial.print(',');
  Serial.print(cRef, 1);        Serial.print(',');
  Serial.print(g_uS, 4);        Serial.print(',');
  Serial.print(filtered, 4);    Serial.print(',');
  Serial.println(status);
}
