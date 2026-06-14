#pragma once
#include "Print.h"

class HardwareSerial : public Print {
public:
  size_t write(uint8_t /*byte*/) override { return 0U; }
  template<typename... Args>
  int printf_P(const char* fmt, Args... /*args*/) {
    (void)fmt;
    return 0;
  }
  // Generic no-op print/println accepting any Arduino overload (string, value, value+base).
  template<typename... Args>
  size_t print(Args... /*args*/) { return 0U; }      // NOLINT(readability-convert-member-functions-to-static)
  template<typename... Args>
  size_t println(Args... /*args*/) { return 0U; }    // NOLINT(readability-convert-member-functions-to-static)
};

inline HardwareSerial Serial;
