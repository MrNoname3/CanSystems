#pragma once

#include "Arduino.h"

class Buffer {
private:
  uint8_t buffer[2048] = {};
  uint16_t pos;
  uint16_t length;

public:
  Buffer();
  Buffer(uint8_t* buf, size_t size);

  bool available() const;
  uint8_t next();
  void reset();

  void add(const uint8_t* buf, size_t size);
};
