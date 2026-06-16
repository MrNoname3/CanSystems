#include "Stream.h"
#include "Buffer.h"
#include "trace.h"
#include <iostream>
#include <Arduino.h>

Stream::Stream() {
  this->expectBuffer = new Buffer();
  this->_error = false;
  this->_written = 0;
}

Stream::~Stream() {
  delete this->expectBuffer;
}

size_t Stream::write(uint8_t b) {
  captured.push_back(b);
  this->_written++;
  TRACE(std::hex << static_cast<unsigned int>(b));
  if(this->expectBuffer->available()) {
    uint8_t expected = this->expectBuffer->next();
    if(expected != b) {
      this->_error = true;
      TRACE("!=" << static_cast<unsigned int>(expected));
    }
  } else {
    this->_error = true;
  }
  TRACE("\n"
        << std::dec);
  return 1;
}

int Stream::available() const {  // NOLINT(readability-convert-member-functions-to-static) mirrors Stream
  return 0;
}

int Stream::read() {             // NOLINT(readability-convert-member-functions-to-static) mirrors Stream
  return -1;
}

bool Stream::error() const {
  return this->_error;
}

void Stream::expect(const uint8_t* buf, size_t size) {
  this->expectBuffer->add(buf, size);
}

uint16_t Stream::length() const {
  return this->_written;
}
