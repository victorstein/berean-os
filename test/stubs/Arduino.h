#pragma once

// Host-test stub for Arduino.h. FontDecompressor.cpp includes it for millis()
// and micros(), which feed FontDecompressor::Stats timing counters only — no
// test asserts on them, so constants are enough. Inert for the suites that
// already put test/stubs on their include path: none of them includes
// <Arduino.h>, and test/pagination exists specifically to keep it out.

#include <cstdint>

inline uint32_t millis() { return 0; }
inline uint32_t micros() { return 0; }
