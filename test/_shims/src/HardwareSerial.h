#pragma once
#include "Print.h"

class HardwareSerial : public Print {
public:
  size_t write(uint8_t /*byte*/) override { return 0U; }
  template<typename... Args>
  int printf_P(const char* fmt, Args... /*args*/) { (void)fmt; return 0; }
};

inline HardwareSerial Serial;
