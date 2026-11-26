/*
  Ultrasonic atomizer (ACEBOTT QB062/QB078 humidifier module) on an Arduino Uno.

  Wiring (module cable is labelled G, V, S on the board):
    module G (GND)  -> Uno GND
    module V (VCC)  -> Uno 5V
    module S        -> Uno D7
  Use the regular header pins (jumper wires), not the ICSP header.

  Behaviour:
    The Uno's onboard LED (labelled L) lights whenever the atomizer signal is on, so you can
    confirm the sketch is running without a multimeter.
    Starts in AUTO mode: mist 5 s on, 5 s off (the manufacturer's example timing).
    Serial monitor at 115200 baud, send one character:
      a = auto cycle     o = on (manual)     f = off
    A hard cutoff turns the atomizer off after MAX_CONTINUOUS_ON_MS in any mode,
    because nothing here senses water level and a piezo atomizer should not run dry.

  The manufacturer lists the S signal as "falling edge trigger" but its own example
  just drives S HIGH = spray, LOW = stop. This sketch follows the example. If the module
  behaves oddly (toggles on each LOW, or the sense looks inverted), set ACTIVE_HIGH to false
  or tell me what you see.
*/

const uint8_t       ATOMIZER_PIN          = 7;
const bool          ACTIVE_HIGH           = false;  // observed: idle LOW ran continuously, so module is active-LOW
const unsigned long ON_MS                 = 5000;    // AUTO mode: mist time
const unsigned long OFF_MS                = 5000;    // AUTO mode: pause time
const unsigned long MAX_CONTINUOUS_ON_MS  = 60000UL; // safety cutoff

enum Mode { MODE_AUTO, MODE_ON, MODE_OFF };
Mode mode = MODE_AUTO;

bool running = false;
unsigned long phaseStart = 0;   // when the current on/off phase began
unsigned long onSince = 0;      // when the atomizer last turned on

void setAtomizer(bool on) {
  digitalWrite(ATOMIZER_PIN, (on == ACTIVE_HIGH) ? HIGH : LOW);
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);   // onboard "L" LED mirrors the atomizer state
  running = on;
  phaseStart = millis();
  if (on) onSince = phaseStart;
}

void printHelp() {
  Serial.println(F("Commands: a = auto cycle, o = on, f = off"));
}

void setup() {
  pinMode(ATOMIZER_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  setAtomizer(false);            // always start off
  Serial.begin(115200);
  printHelp();
  Serial.println(F("Mode: AUTO"));
}

void loop() {
  // --- serial commands ---
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'a')      { mode = MODE_AUTO; Serial.println(F("Mode: AUTO")); }
    else if (c == 'o') { mode = MODE_ON;   Serial.println(F("Mode: ON")); }
    else if (c == 'f') { mode = MODE_OFF;  Serial.println(F("Mode: OFF")); }
  }

  unsigned long now = millis();

  // --- safety cutoff ---
  if (running && (now - onSince >= MAX_CONTINUOUS_ON_MS)) {
    setAtomizer(false);
    mode = MODE_OFF;
    Serial.println(F("Safety cutoff reached, atomizer off. Send 'a' or 'o' to restart."));
    return;
  }

  // --- mode logic ---
  switch (mode) {
    case MODE_AUTO:
      if (running && now - phaseStart >= ON_MS)        setAtomizer(false);
      else if (!running && now - phaseStart >= OFF_MS) setAtomizer(true);
      break;
    case MODE_ON:
      if (!running) setAtomizer(true);
      break;
    case MODE_OFF:
      if (running) setAtomizer(false);
      break;
  }
}
