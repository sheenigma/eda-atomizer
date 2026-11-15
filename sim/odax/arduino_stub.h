#pragma once
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string>
#include <deque>
#include <vector>
#define A0 14
#define A1 15
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define EXTERNAL 0
#define LED_BUILTIN 13
#define F(x) (x)
extern unsigned long fake_ms;
inline unsigned long millis() { return fake_ms; }
inline unsigned long micros() { return fake_ms * 1000UL; }
void analogReference(int);
int analogRead(uint8_t pin);
void pinMode(uint8_t, uint8_t);
void digitalWrite(uint8_t pin, uint8_t v);
struct SerialStub {
  std::string out; std::deque<char> in;
  void begin(long) {}
  int available() { return (int)in.size(); }
  char read() { char c = in.front(); in.pop_front(); return c; }
  void print(const char* s) { out += s; }
  void print(char c) { out += c; }
  void print(int v) { out += std::to_string(v); }
  void print(unsigned long v) { out += std::to_string(v); }
  void print(double v, int d = 2) { char b[40]; if (isnan(v)) snprintf(b, 40, "nan"); else snprintf(b, 40, "%.*f", d, v); out += b; }
  void println(const char* s) { out += s; out += "\n"; }
  void println(int v) { print(v); out += "\n"; }
};
extern SerialStub Serial;
