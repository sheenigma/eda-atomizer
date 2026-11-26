/*
  atomizer_probe.ino  -  find out how the atomizer module's S pin really behaves

  Wiring: atomizer V -> 5V, G -> GND, S -> D7. Nothing else is needed (no EDA board).
  Serial Monitor at 115200, "Newline" line ending. Type one letter, press Return,
  and WRITE DOWN what the atomizer does within a few seconds (mist / no mist).

    h   D7 = HIGH (held)
    l   D7 = LOW  (held)
    z   D7 = floating input (Hi-Z)
    p   falling-edge pulse: HIGH, then LOW for 100 ms, then HIGH again
    n   rising-edge pulse:  LOW, then HIGH for 100 ms, then LOW again
    ?   help

  Reading the result:
    h stops it and l starts it          -> active-LOW level (set ACTIVE_HIGH = false)
    l stops it and h starts it          -> active-HIGH level (set ACTIVE_HIGH = true)
    h and l change nothing, but p (or n) flips it each time
                                        -> edge toggle: each pulse switches it on/off
    nothing changes it                  -> S is not controlling it: re-check the G/V/S order
                                           on the module and try power-cycling it
*/
const uint8_t PIN_S = 7;

static void say(const char* m) {
  Serial.print(millis() / 1000.0, 1);
  Serial.print(" s  ");
  Serial.println(m);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_S, INPUT);   // start floating on purpose: this is the power-up condition
  Serial.println(F("# atomizer_probe. h l z p n ?"));
  say("D7 floating (Hi-Z). Is the atomizer running right now?");
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'h') { pinMode(PIN_S, OUTPUT); digitalWrite(PIN_S, HIGH); say("D7 = HIGH"); }
  else if (c == 'l') { pinMode(PIN_S, OUTPUT); digitalWrite(PIN_S, LOW); say("D7 = LOW"); }
  else if (c == 'z') { pinMode(PIN_S, INPUT); say("D7 floating (Hi-Z)"); }
  else if (c == 'p') {
    pinMode(PIN_S, OUTPUT); digitalWrite(PIN_S, HIGH); delay(50);
    digitalWrite(PIN_S, LOW); delay(100); digitalWrite(PIN_S, HIGH);
    say("falling-edge pulse sent, D7 now HIGH");
  }
  else if (c == 'n') {
    pinMode(PIN_S, OUTPUT); digitalWrite(PIN_S, LOW); delay(50);
    digitalWrite(PIN_S, HIGH); delay(100); digitalWrite(PIN_S, LOW);
    say("rising-edge pulse sent, D7 now LOW");
  }
  else if (c == '?') Serial.println(F("# h l z p n ?"));
}
