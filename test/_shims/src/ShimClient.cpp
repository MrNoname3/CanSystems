#include "ShimClient.h"
#include "trace.h"
#include <iostream>
#include <Arduino.h>
#include <ctime>
#include <string.h>
#include <vector>
#include "SPI.h"
#include "esp32CanModel.h"
#include "esp_intr_alloc.h"

static uint32_t fakeMillisValue = 0U;
static bool fakeMillisActive = false;
static uint32_t fakeMicrosValue = 0U;
static bool fakeMicrosActive = false;
static std::vector<Pulse> pulseLog;                  // Edges of the recorded pin, in order.
static uint8_t pulsePin = 0U;                        // Pin being recorded.
static bool pulseRecording = false;
static uint8_t pinModes[256] = {};
static uint8_t pinValues[256] = {};
static uint16_t analogReadValue = 0U;
static void (*isrTable[256])() = {};                 // Handlers stored by attachInterrupt().

FlagRegister EIFR;                                   // AVR external interrupt flag register stand-in.

void setFakeMillis(uint32_t t) {
  fakeMillisValue = t;
  fakeMillisActive = true;
}
void clearFakeMillis() { fakeMillisActive = false; }
void setFakeMicros(uint32_t t) {
  fakeMicrosValue = t;
  fakeMicrosActive = true;
}
void clearFakeMicros() { fakeMicrosActive = false; }

void recordPulsesOn(uint8_t pin) {
  pulseLog.clear();
  pulsePin = pin;
  pulseRecording = true;
}
void stopRecordingPulses() { pulseRecording = false; }
const Pulse* recordedPulses() { return pulseLog.data(); }
uint32_t recordedPulseCount() { return static_cast<uint32_t>(pulseLog.size()); }
void setAnalogReadValue(uint16_t v) { analogReadValue = v; }
uint8_t getDigitalWriteValue(uint8_t pin) { return pinValues[pin]; }
uint8_t getPinMode(uint8_t pin) { return pinModes[pin]; }
void triggerInterrupt(uint8_t pin) {
  if(isrTable[pin] != nullptr) { isrTable[pin](); }
}
void resetGpioState() {
  memset(pinModes, 0, sizeof(pinModes));
  memset(pinValues, 0, sizeof(pinValues));
  memset(isrTable, 0, sizeof(isrTable));
  analogReadValue = 0U;
  EIFR.clearAll();
  pulseLog.clear();
  pulseRecording = false;
}

extern "C" {
uint32_t millis(void) {
  if(fakeMillisActive) { return fakeMillisValue; }
  return static_cast<uint32_t>(time(nullptr)) * 1000U;
}
uint32_t micros(void) {
  if(fakeMicrosActive) { return fakeMicrosValue; }
  return millis() * 1000U;
}
// The wait itself is skipped; what it was worth is added to the pulse the pin is holding.
void delayMicroseconds(uint32_t us) {
  if(pulseRecording && !pulseLog.empty()) { pulseLog.back().microseconds += us; }
}
void pinMode(uint8_t pin, uint8_t mode) { pinModes[pin] = mode; }
void digitalWrite(uint8_t pin, uint8_t val) {
  pinValues[pin] = val;
  if(pulseRecording && (pin == pulsePin)) { pulseLog.push_back(Pulse{ val, 0U }); }
}
int digitalRead(uint8_t pin) { return pinValues[pin]; }
uint16_t analogRead(uint8_t /*pin*/) { return analogReadValue; }
void analogWrite(uint8_t pin, int val) { pinValues[pin] = static_cast<uint8_t>(val); }
void attachInterrupt(uint8_t pin, void (*fn)(), uint8_t /*mode*/) { isrTable[pin] = fn; }
void detachInterrupt(uint8_t pin) { isrTable[pin] = nullptr; }
int16_t digitalPinToInterrupt(uint8_t pin) { return pin; }
void cli() {}
void sei() {}
void noInterrupts() {}
void interrupts() {}
}

ShimClient::ShimClient() {
  this->responseBuffer = new Buffer();
  this->expectBuffer = new Buffer();
  this->_allowConnect = true;
  this->_connected = false;
  this->_error = false;
  this->expectAnything = true;
  this->_received = 0;
  this->_writesToFail = 0;
  this->_expectedPort = 0;
  this->_expectedHost = nullptr;
}

ShimClient::~ShimClient() {
  delete this->responseBuffer;
  delete this->expectBuffer;
}

bool ShimClient::connect(IPAddress /*ip*/, uint16_t port) {
  if(this->_allowConnect) {
    this->_connected = true;
  }
  if(this->_expectedPort != 0) {
    // if (memcmp(ip,this->_expectedIP,4) != 0) {
    //     TRACE( "ip mismatch\n");
    //     this->_error = true;
    // }
    if(port != this->_expectedPort) {
      TRACE("port mismatch\n");
      this->_error = true;
    }
  }
  return this->_connected;
}
bool ShimClient::connect(const char* host, uint16_t port) {
  if(this->_allowConnect) {
    this->_connected = true;
  }
  if(this->_expectedPort != 0) {
    if(strcmp(host, this->_expectedHost) != 0) {
      TRACE("host mismatch\n");
      this->_error = true;
    }
    if(port != this->_expectedPort) {
      TRACE("port mismatch\n");
      this->_error = true;
    }
  }
  return this->_connected;
}
size_t ShimClient::write(uint8_t b) {
  this->_received += 1;
  TRACE(std::hex << static_cast<unsigned int>(b));
  if(!this->expectAnything) {
    if(this->expectBuffer->available()) {
      uint8_t expected = this->expectBuffer->next();
      if(expected != b) {
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
void ShimClient::failNextWrites(uint16_t count) {
  this->_writesToFail = count;
}

void ShimClient::checkExpected(uint8_t actual) {
  if(this->expectAnything) {
    return;
  }
  if(!this->expectBuffer->available()) {
    this->_error = true;
    return;
  }
  const uint8_t expected = this->expectBuffer->next();
  if(expected != actual) {
    this->_error = true;
    TRACE("!=" << static_cast<unsigned int>(expected));
  }
}

size_t ShimClient::write(const uint8_t* buf, size_t size) {
  if(this->_writesToFail > 0U) {
    this->_writesToFail--;
    return 0U;
  }
  this->_received += size;
  TRACE("[" << std::dec << static_cast<unsigned int>(size) << "] ");
  for(size_t i = 0; i < size; i++) {
    if(i > 0) {
      TRACE(":");
    }
    TRACE(std::hex << static_cast<unsigned int>(buf[i]));

    this->checkExpected(buf[i]);
  }
  TRACE("\n"
        << std::dec);
  return size;
}
int ShimClient::available() {
  return static_cast<int>(this->responseBuffer->available());
}
int ShimClient::read() {
  return static_cast<int>(this->responseBuffer->next());
}
int ShimClient::read(uint8_t* buf, size_t size) { // NOLINT(readability-non-const-parameter)
  // Only what it actually holds, as a socket does: a caller asking for more has to come back.
  size_t taken = 0U;
  while((taken < size) && this->responseBuffer->available()) {
    buf[taken] = static_cast<uint8_t>(this->read());
    taken++;
  }
  return static_cast<int>(taken);
}
int ShimClient::peek() {
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

// --- SPI stand-in (SPI.h) ---
Mcp2515Model mcp2515;
SPIClass SPI;

namespace {
  SpiFlashModel* spiFlash = nullptr;                  // Flash on the bus, or nullptr when none is.
  uint8_t spiFlashSelectPin = 0U;                     // Its chip-select pin.

  /// @brief Whether the flash is the device the line currently points at.
  bool spiFlashSelected() {
    return (spiFlash != nullptr) && (getDigitalWriteValue(spiFlashSelectPin) == LOW);
  }
} // namespace

void attachSpiFlash(SpiFlashModel* model, uint8_t chipSelectPin) {
  spiFlash = model;
  spiFlashSelectPin = chipSelectPin;
}

void SPIClass::beginTransaction(SPISettings /*settings*/) {
  // Both models are told: the flash raises its chip select only after this call, so which device
  // the transaction belongs to is not known yet.
  mcp2515.beginMessage();
  if(spiFlash != nullptr) { spiFlash->beginMessage(); }
}

uint8_t SPIClass::transfer(uint8_t out) {
  return spiFlashSelected() ? spiFlash->transfer(out) : mcp2515.transfer(out);
}

// --- ESP32 CAN peripheral stand-in (esp32CanModel.h, esp_intr_alloc.h) ---
Esp32CanModel esp32Can;
Esp32IntrRegistration esp32Intr;
uint32_t* esp32CanRegisterFile() { return esp32Can.file(); }
void esp32CanOnAccess(uint8_t address, bool isWrite) { esp32Can.onAccess(address, isWrite); }
