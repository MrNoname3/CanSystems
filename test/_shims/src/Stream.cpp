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

int Stream::available() {        // NOLINT(readability-convert-member-functions-to-static) mirrors Stream
  return 0;
}

int Stream::read() {             // NOLINT(readability-convert-member-functions-to-static) mirrors Stream
  return -1;
}

int Stream::peek() {             // NOLINT(readability-convert-member-functions-to-static) mirrors Stream
  return -1;
}

void Stream::flush() {}

// Reads until the buffer is full or the source runs dry. The Arduino original also waits for a
// timeout; tests need a deterministic answer, so an empty source ends the read immediately.
size_t Stream::readBytes(uint8_t* buffer, size_t length) {
  size_t count = 0U;
  while(count < length) {
    const int value = read();
    if(value < 0) { break; }
    buffer[count++] = static_cast<uint8_t>(value);
  }
  return count;
}

void Stream::setTimeout(unsigned long /*timeout*/) {}

bool Stream::error() const {
  return this->_error;
}

void Stream::expect(const uint8_t* buf, size_t size) {
  this->expectBuffer->add(buf, size);
}

uint16_t Stream::length() const {
  return this->_written;
}
