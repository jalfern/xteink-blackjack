#pragma once
#include <stdint.h>

// Xteink X3 keys are NOT GPIO switches. Six of them share two ADC pins through
// a resistor ladder; you read the voltage to tell which key is down. The seventh
// key (power) is a plain digital input.
//
//   GPIO1 (ADC) : Back ~3512  Confirm ~2694  Left ~1493  Right ~5
//   GPIO2 (ADC) : Up ~2242                      Down ~5
//   GPIO3       : Power, active LOW, INPUT_PULLUP
//
// Ranges and measured values are from the PapyriX InputManager, which publishes
// its real-device samples. The idle line is pulled high (~4095), above the top
// of every range, so an idle pin never matches a key.
namespace keys {

constexpr int PIN_ADC1  = 1;
constexpr int PIN_ADC2  = 2;
constexpr int PIN_POWER = 3;

enum class Key : uint8_t {
  NONE = 0, BACK, CONFIRM, LEFT, RIGHT, UP, DOWN, POWER, COUNT
};

void begin();

// Bitmask of currently-held keys.
uint32_t held();

// Blocks until a fresh press, then returns it.
Key waitKey();

// Non-blocking: returns NONE if nothing new since the last call.
Key pollKey();

// True once POWER has been held continuously for ms. Advanced inside pollKey(),
// which is the only place guaranteed to run continuously - the previous design
// checked POWER once per loop() iteration, but every screen blocks in
// waitKey(), so during actual play it was never sampled at all.
bool powerHeldFor(uint32_t ms);

const char* name(Key k);

} // namespace keys
