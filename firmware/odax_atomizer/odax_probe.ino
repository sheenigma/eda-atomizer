/*
  ODAX EDA probe: streams the raw sensor output so you can see how this board behaves.
  Open Tools > Serial Plotter at 115200 baud.

  WIRING: ODAX VCC -> Uno 5V, ODAX GND -> Uno GND, ODAX OUT -> A0. Atomizer not needed.

  WHAT TO MEASURE (write the numbers down, odax_atomizer.ino needs them)
    1. Electrodes unplugged / not touching skin: note the reading. This is the "no contact" level.
    2. Straps on two fingers of one hand (the ODAX tutorial uses index and middle finger).
       Sit still for 1-2 minutes. Note how far the reading drifts while it settles.
    3. POLARITY: take a deep breath or clap, or press the straps harder.
       Value goes UP when you are aroused / the skin gets more conductive  -> POLARITY = +1
       Value goes DOWN                                                      -> POLARITY = -1
    4. Noise: peak-to-peak of the resting line over 10 s, and the size of a typical response.
       A good TRIGGER_RISE_COUNTS is about half the size of a typical response, and well above the noise.
  1 count is about 4.9 mV when the 5V pin is 5.0 V (ADC reference = supply).
*/

const uint8_t PIN_SIG = A0;
const uint8_t OVERSAMPLE = 16;
const unsigned long PERIOD_MS = 50;   // 20 Hz

unsigned long nextMs;

void setup() {
  Serial.begin(115200);
  (void)analogRead(PIN_SIG);
  nextMs = millis();
}

void loop() {
  if ((long)(millis() - nextMs) < 0) return;
  nextMs += PERIOD_MS;

  uint32_t sum = 0;
  for (uint8_t i = 0; i < OVERSAMPLE; i++) sum += analogRead(PIN_SIG);
  Serial.print("counts:");
  Serial.println((float)sum / OVERSAMPLE, 1);
}
