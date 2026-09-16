#include "input.h"
#include <Arduino.h>

namespace keys {

// Straight from PapyriX InputManager, including the deliberate asymmetry of the
// two ladders. Buckets are (low, high]; the top bound of range 0 is the "no key"
// floor, and an idle pin reads ~4095 so it matches nothing.
static const int R1[5] = { 3900, 3100, 2090, 750, INT32_MIN };  // Back Conf Left Right
static const int R2[3] = { 3900, 1120, INT32_MIN };             // Up Down

static uint32_t s_lastMask = 0;
static uint32_t s_powerSince = 0;

bool powerHeldFor(uint32_t ms) {
  return s_powerSince != 0 && (millis() - s_powerSince) >= ms;
}

void begin() {
  pinMode(PIN_ADC1, INPUT);
  pinMode(PIN_ADC2, INPUT);
  pinMode(PIN_POWER, INPUT_PULLUP);
  analogSetAttenuation(ADC_11db);   // full-scale ~3.3 V, needed for these numbers
}

// The SAR ADC shares ADC1 with the battery gauge on GPIO0; the first sample
// after switching channels is contaminated by the previous channel's
// sample-and-hold charge. Upstream discards it and we must too, or the first
// read of the loop reports a phantom keypress.
static int readAdc(int pin) {
  (void)analogRead(pin);
  delayMicroseconds(20);
  return analogRead(pin);
}

static int bucket(int v, const int* ranges, int n) {
  for (int i = 0; i < n; i++) {
    if (ranges[i + 1] < v && v <= ranges[i]) return i;
  }
  return -1;
}

uint32_t held() {
  uint32_t m = 0;
  int v1 = readAdc(PIN_ADC1);
  int b1 = bucket(v1, R1, 4);
  if (b1 >= 0) m |= 1u << (static_cast<int>(Key::BACK) + b1 - 1);

  int v2 = readAdc(PIN_ADC2);
  int b2 = bucket(v2, R2, 2);
  if (b2 >= 0) m |= 1u << (static_cast<int>(Key::UP) + b2 - 1);

  if (digitalRead(PIN_POWER) == LOW) m |= 1u << (static_cast<int>(Key::POWER) - 1);
  return m;
}

Key pollKey() {
  // Two agreeing samples ~4 ms apart. The ladder bounces far more than a
  // GPIO switch would, especially between adjacent buckets.
  uint32_t a = held();
  delay(4);
  uint32_t b = held();
  if (a != b) { a = b; delay(4); a = held(); }

  uint32_t fresh = b & ~s_lastMask;
  s_lastMask = b;

  // POWER hold tracking lives here, not in the app layer: pollKey() is the one
  // place that keeps running while a screen is blocked waiting for input.
  if (b & (1u << (static_cast<int>(Key::POWER) - 1))) {
    if (s_powerSince == 0) s_powerSince = millis();
  } else {
    s_powerSince = 0;
  }

  for (int i = 1; i < static_cast<int>(Key::COUNT); i++) {
    if (fresh & (1u << (i - 1))) return static_cast<Key>(i);
  }
  return Key::NONE;
}

Key waitKey() {
  Key k;
  do { k = pollKey(); } while (k == Key::NONE);
  return k;
}

const char* name(Key k) {
  switch (k) {
    case Key::BACK:    return "BACK";
    case Key::CONFIRM: return "CONFIRM";
    case Key::LEFT:    return "LEFT";
    case Key::RIGHT:   return "RIGHT";
    case Key::UP:      return "UP";
    case Key::DOWN:    return "DOWN";
    case Key::POWER:   return "POWER";
    default:           return "-";
  }
}

} // namespace keys
