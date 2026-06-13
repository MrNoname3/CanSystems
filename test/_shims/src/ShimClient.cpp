#include "ShimClient.h"
#include "trace.h"
#include <iostream>
#include <Arduino.h>
#include <ctime>
#include <string.h>

static uint32_t fakeMillisValue = 0U;
static bool     fakeMillisActive = false;
static uint8_t  pinModes[256]  = {};
static uint8_t  pinValues[256] = {};
static uint16_t analogReadValue = 0U;
static void (*isrTable[256])() = {};                 // Handlers stored by attachInterrupt().

uint8_t EIFR = 0U;                                   // AVR external interrupt flag register stand-in.

void setFakeMillis(uint32_t t)     { fakeMillisValue = t; fakeMillisActive = true; }
void clearFakeMillis()             { fakeMillisActive = false; }
void setAnalogReadValue(uint16_t v){ analogReadValue = v; }
uint8_t getDigitalWriteValue(uint8_t pin) { return pinValues[pin]; }
uint8_t getPinMode(uint8_t pin)    { return pinModes[pin]; }
void triggerInterrupt(uint8_t pin) {
  if (isrTable[pin] != nullptr) { isrTable[pin](); }
}
void resetGpioState() {
  memset(pinModes,  0, sizeof(pinModes));
  memset(pinValues, 0, sizeof(pinValues));
  memset(isrTable,  0, sizeof(isrTable));
  analogReadValue = 0U;
  EIFR = 0U;
}

extern "C" {
uint32_t millis(void) {
  if (fakeMillisActive) { return fakeMillisValue; }
  return static_cast<uint32_t>(time(nullptr)) * 1000U;
}
void     pinMode(uint8_t pin, uint8_t mode)       { pinModes[pin]  = mode; }
void     digitalWrite(uint8_t pin, uint8_t val)   { pinValues[pin] = val; }
int      digitalRead(uint8_t pin)                 { return pinValues[pin]; }
uint16_t analogRead(uint8_t /*pin*/)              { return analogReadValue; }
void     analogWrite(uint8_t pin, int val)        { pinValues[pin] = static_cast<uint8_t>(val); }
void     attachInterrupt(uint8_t pin, void (*fn)(), uint8_t /*mode*/) { isrTable[pin] = fn; }
void     detachInterrupt(uint8_t pin) { isrTable[pin] = nullptr; }
uint8_t  digitalPinToInterrupt(uint8_t pin)       { return pin; }
void     cli() {}
void     sei() {}
void     noInterrupts() {}
void     interrupts() {}
}

ShimClient::ShimClient() {
  this->responseBuffer = new Buffer();
  this->expectBuffer = new Buffer();
  this->_allowConnect = true;
  this->_connected = false;
  this->_error = false;
  this->expectAnything = true;
  this->_received = 0;
  this->_expectedPort = 0;
  this->_expectedHost = nullptr;
}

bool ShimClient::connect(IPAddress /*ip*/, uint16_t port) {
  if (this->_allowConnect) {
    this->_connected = true;
  }
  if (this->_expectedPort != 0) {
    // if (memcmp(ip,this->_expectedIP,4) != 0) {
    //     TRACE( "ip mismatch\n");
    //     this->_error = true;
    // }
    if (port != this->_expectedPort) {
      TRACE("port mismatch\n");
      this->_error = true;
    }
  }
  return this->_connected;
}
bool ShimClient::connect(const char* host, uint16_t port) {
  if (this->_allowConnect) {
    this->_connected = true;
  }
  if (this->_expectedPort != 0) {
    if (strcmp(host, this->_expectedHost) != 0) {
      TRACE("host mismatch\n");
      this->_error = true;
    }
    if (port != this->_expectedPort) {
      TRACE("port mismatch\n");
      this->_error = true;
    }
  }
  return this->_connected;
}
size_t ShimClient::write(uint8_t b) {
  this->_received += 1;
  TRACE(std::hex << static_cast<unsigned int>(b));
  if (!this->expectAnything) {
    if (this->expectBuffer->available()) {
      uint8_t expected = this->expectBuffer->next();
      if (expected != b) {
        this->_error = true;
        TRACE("!=" << (unsigned int)expected);
      }
    } else {
      this->_error = true;
    }
  }
  TRACE("\n"
        << std::dec);
  return 1;
}
size_t ShimClient::write(const uint8_t* buf, size_t size) {
  this->_received += size;
  TRACE("[" << std::dec << static_cast<unsigned int>(size) << "] ");
  for (size_t i = 0; i < size; i++) {
    if (i > 0) {
      TRACE(":");
    }
    TRACE(std::hex << static_cast<unsigned int>(buf[i]));

    if (!this->expectAnything) {
      if (this->expectBuffer->available()) {
        uint8_t expected = this->expectBuffer->next();
        if (expected != buf[i]) {
          this->_error = true;
          TRACE("!=" << static_cast<unsigned int>(expected));
        }
      } else {
        this->_error = true;
      }
    }
  }
  TRACE("\n"
        << std::dec);
  return size;
}
int16_t ShimClient::available() {
  return static_cast<int16_t>(this->responseBuffer->available());
}
int16_t ShimClient::read() {
  return static_cast<int16_t>(this->responseBuffer->next());
}
int16_t ShimClient::read(uint8_t* buf, size_t size) { // NOLINT(readability-non-const-parameter)
  for (size_t i = 0; i < size; i++) {
    buf[i] = static_cast<uint8_t>(this->read());
  }
  return static_cast<int16_t>(size);
}
int16_t ShimClient::peek() {
  return 0;
}
void ShimClient::flush() {}
void ShimClient::stop() {
  this->setConnected(false);
}
uint8_t ShimClient::connected() {
  return this->_connected ? 1U : 0U;
}
ShimClient::operator bool() {
  return true;
}

ShimClient* ShimClient::respond(const uint8_t* buf, size_t size) {
  this->responseBuffer->add(buf, size);
  return this;
}

ShimClient* ShimClient::expect(const uint8_t* buf, size_t size) {
  this->expectAnything = false;
  this->expectBuffer->add(buf, size);
  return this;
}

void ShimClient::setConnected(bool b) {
  this->_connected = b;
}
void ShimClient::setAllowConnect(bool b) {
  this->_allowConnect = b;
}

bool ShimClient::error() const {
  return this->_error;
}

uint16_t ShimClient::received() const {
  return this->_received;
}

void ShimClient::expectConnect(IPAddress ip, uint16_t port) {
  this->_expectedIP = ip;
  this->_expectedPort = port;
}

void ShimClient::expectConnect(const char* host, uint16_t port) {
  this->_expectedHost = host;
  this->_expectedPort = port;
}
