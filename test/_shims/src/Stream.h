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
  Stream(const Stream&) = delete;
  Stream& operator=(const Stream&) = delete;
  size_t write(uint8_t);

  [[nodiscard]] bool error() const;
  void expect(const uint8_t* buf, size_t size);
  [[nodiscard]] uint16_t length() const;
};
