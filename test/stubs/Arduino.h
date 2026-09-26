#pragma once

// Host-test stub for Arduino.h.
//
// FontDecompressor.cpp includes it for millis() and micros(), which feed
// FontDecompressor::Stats timing counters only -- no test asserts on them, so
// constants are enough.
//
// String exists for PersistableStore.cpp and for the Storage fake in
// HalStorage.h, which is how test/pagination reaches this file too. It is not
// Arduino's String: it carries only what those two need. begin()/end() are what
// ArduinoJson's deserializeJson reads a class through (IteratorReader.hpp), and
// the two write() overloads are what serializeJson appends through (Writer.hpp).

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

inline uint32_t millis() { return 0; }
inline uint32_t micros() { return 0; }

class String {
 public:
  using const_iterator = std::string::const_iterator;

  String() = default;
  String(const char* s) : text(s ? s : "") {}
  String(std::string s) : text(std::move(s)) {}

  const char* c_str() const { return text.c_str(); }
  size_t length() const { return text.size(); }
  bool isEmpty() const { return text.empty(); }
  const_iterator begin() const { return text.begin(); }
  const_iterator end() const { return text.end(); }

  size_t write(uint8_t c) {
    text.push_back(static_cast<char>(c));
    return 1;
  }
  size_t write(const uint8_t* p, size_t n) {
    text.append(reinterpret_cast<const char*>(p), n);
    return n;
  }

 private:
  std::string text;
};
