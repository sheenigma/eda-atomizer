#include "arduino_stub.h"
#include <random>
#include <map>
unsigned long fake_ms = 0;
SerialStub Serial;
static std::mt19937 rng(7);
static std::normal_distribution<double> nd(0.0, 1.0);
static std::vector<std::pair<unsigned long,int>> atom_events;
static int atom_state = 0;
// skin conductance-like arousal trace, same shape as before; contact lost 130-140 s
double G(double t) {
  double g = 5.0 + 0.003 * t;
  if (t >= 140) g = 6.0 + 0.003 * (t - 140);
  auto scr = [&](double t0, double amp) {
    if (t < t0) return 0.0;
    if (t < t0 + 2) return amp * (t - t0) / 2.0;
    return amp * exp(-(t - t0 - 2) / 6.0);
  };
  return g + scr(70, 0.8) + scr(100, 0.8) + scr(115, 0.8);
}
bool contact(double t) { return !(t < 5 || (t >= 130 && t < 140)); }
void analogReference(int) {}
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t v) {
  if (pin == 7 && (int)v != atom_state) { atom_state = v; atom_events.push_back({fake_ms, (int)v}); }
}
int analogRead(uint8_t) {
  double t = fake_ms / 1000.0;
  double c;
  if (!contact(t)) c = (SIGN > 0) ? 10.0 : 1020.0;       // open-circuit level
  else c = 450.0 + SIGN * 15.0 * (G(t) - 5.0);            // counts move 15 per uS
  c += nd(rng) * 1.0;
  int v = (int)lround(c); if (v < 0) v = 0; if (v > 1023) v = 1023;
  return v;
}
#define ARDUINO_ARCH_AVR 1
#include SKETCH
int main() {
  setup();
  std::map<unsigned long,char> cmds = {{200000,'t'},{220000,'o'},{240000,'a'}};
  for (fake_ms = 0; fake_ms <= 260000; fake_ms++) {
    if (cmds.count(fake_ms)) Serial.in.push_back(cmds[fake_ms]);
    loop();
  }
  FILE* f = fopen(LOGNAME, "w"); fputs(Serial.out.c_str(), f); fclose(f);
  printf("atomizer events (s -> state):");
  for (auto& e : atom_events) printf("  %.1f->%d", e.first/1000.0, e.second);
  printf("\n");
  return 0;
}
