#pragma once
#include "Print.h"

class HardwareSerial : public Print {
public:
  size_t write(uint8_t) override { return 0U; }
};

inline HardwareSerial Serial;
