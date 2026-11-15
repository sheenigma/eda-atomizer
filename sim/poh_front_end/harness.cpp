#include "arduino_stub.h"
#include <random>
#include <sstream>
#include <map>
unsigned long fake_ms = 0;
SerialStub Serial;
static std::mt19937 rng(1);
static std::normal_distribution<double> nd(0.0, 1.0);
static std::vector<std::pair<unsigned long,int>> atom_events;
static int atom_state = 0;
double G_uS(double t) {
  if (t < 5) return 0.0;
  if (t >= 130 && t < 140) return 0.0;
  double g = 5.0 + 0.003 * t;
  if (t >= 140) g = 6.0 + 0.003 * (t - 140);
  auto scr = [&](double t0, double amp) {
    if (t < t0) return 0.0;
    if (t < t0 + 2) return amp * (t - t0) / 2.0;
    return amp * exp(-(t - t0 - 2) / 6.0);
  };
  g += scr(70, 0.8) + scr(100, 0.8) + scr(115, 0.8);
  return g;
}
void analogReference(int) {}
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t v) {
  if (pin == 7 && (int)v != atom_state) { atom_state = v; atom_events.push_back({fake_ms, (int)v}); }
}
int analogRead(uint8_t pin) {
  double t = fake_ms / 1000.0;
  double counts = (pin == A1) ? 512.0 : 1024.0 / (2.0 + G_uS(t));
  counts += nd(rng) * 1.0;
  int c = (int)lround(counts); if (c < 0) c = 0; if (c > 1023) c = 1023;
  return c;
}
#define ARDUINO_ARCH_AVR 1
#include "/mnt/user-data/outputs/eda_atomizer/eda_atomizer.ino"
int main() {
  setup();
  std::map<unsigned long,char> cmds = {{200000,'t'},{220000,'o'},{240000,'a'}};
  for (fake_ms = 0; fake_ms <= 260000; fake_ms++) {
    if (cmds.count(fake_ms)) Serial.in.push_back(cmds[fake_ms]);
    loop();
  }
  FILE* f = fopen("serial.log", "w"); fputs(Serial.out.c_str(), f); fclose(f);
  printf("atomizer events (ms -> state):\n");
  for (auto& e : atom_events) printf("  %8lu -> %d\n", e.first, e.second);
  return 0;
}
