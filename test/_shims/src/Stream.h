#pragma once

#include "Arduino.h"
#include "Buffer.h"

class Stream {
private:
  Buffer* expectBuffer;
  bool _error;
  uint16_t _written;

public:
  Stream();
  size_t write(uint8_t);

  bool error();
  void expect(uint8_t* buf, size_t size);
  uint16_t length();
};
