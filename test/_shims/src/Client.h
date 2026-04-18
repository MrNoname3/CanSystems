#pragma once
#include "IPAddress.h"

class Client {
public:
  virtual bool connect(IPAddress ip, uint16_t port) = 0;
  virtual bool connect(const char* host, uint16_t port) = 0;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* buf, size_t size) = 0;
  virtual int16_t available() = 0;
  virtual int16_t read() = 0;
  virtual int16_t read(uint8_t* buf, size_t size) = 0;
  virtual int16_t peek() = 0;
  virtual void flush() = 0;
  virtual void stop() = 0;
  virtual uint8_t connected() = 0;
  virtual operator bool() = 0;
};

