#pragma once
#include <stdint.h>
#include <math.h>

// HomeKit works in Celsius; the Balboa spa uses raw integer Fahrenheit.
inline float   fToC(uint8_t f) { return (f - 32) / 1.8f; }
inline uint8_t cToF(float c)   { return (uint8_t)lroundf(c * 1.8f + 32.0f); }
