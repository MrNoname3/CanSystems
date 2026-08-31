#pragma once
#include <stdint.h>
#include <deque>

class TwoWire {
public:
  // begin() reinitialises the peripheral. On AVR it runs twi_init(), which rewrites TWBR from
  // TWI_FREQ and so discards any clock configured beforehand; the timeout is left alone.
  void begin() {
    clockHz = defaultClockHz;
    clockSetAfterBegin = false;
  }

  void setClock(uint32_t hz) {
    clockHz = hz;
    clockSetAfterBegin = true;
  }

  void setWireTimeout(uint32_t timeout, bool /*reset*/) { timeoutUs = timeout; }
  void beginTransmission(uint8_t /*addr*/) {}

  [[nodiscard]] uint8_t endTransmission() const { return txResult; }

  // Answers with what is actually available, as TwoWire does: a device that NACKs part way
  // through, or a bus timeout, leaves the caller with fewer bytes than it asked for. Returning
  // all-or-nothing here would hide every short-read path from the suite.
  uint8_t requestFrom(uint8_t /*addr*/, uint8_t n) { // NOLINT(readability-convert-member-functions-to-static) reads the instance's queue, like read() below
    const size_t queued = readQueue.size();
    return static_cast<uint8_t>(queued < static_cast<size_t>(n) ? queued : n);
  }

  uint8_t read() { // NOLINT(readability-convert-member-functions-to-static)
    if(readQueue.empty()) { return 0xFFU; }
    uint8_t b = readQueue.front();
    readQueue.pop_front();
    return b;
  }

  uint8_t write(uint8_t /*byte*/) { return 1U; }           // NOLINT(readability-convert-member-functions-to-static)
  uint8_t write(const uint8_t* /*buf*/, uint8_t n) { return n; }            // NOLINT(readability-convert-member-functions-to-static)
  uint8_t available() { return static_cast<uint8_t>(readQueue.size()); }

  void addReadByte(uint8_t b) { readQueue.push_back(b); }
  void addReadBytes(const uint8_t* d, uint8_t n) { // NOLINT(readability-convert-member-functions-to-static)
    for(uint8_t i = 0U; i < n; ++i) { readQueue.push_back(d[i]); }
  }
  void setEndTransmissionResult(uint8_t r) { txResult = r; }

  // ---- bus configuration observers ----
  [[nodiscard]] uint32_t getClock() const { return clockHz; }
  [[nodiscard]] uint32_t getWireTimeout() const { return timeoutUs; }
  /// @brief True when the clock was configured after begin(), i.e. it actually took effect.
  [[nodiscard]] bool isClockSetAfterBegin() const { return clockSetAfterBegin; }

  void reset() {
    readQueue.clear();
    txResult = 0U;
    clockHz = defaultClockHz;
    timeoutUs = 0U;
    clockSetAfterBegin = false;
  }

private:
  static constexpr uint32_t defaultClockHz = 100000U;   // AVR TWI_FREQ, applied by twi_init().

  std::deque<uint8_t> readQueue;
  uint8_t txResult = 0U;
  uint32_t clockHz = defaultClockHz;
  uint32_t timeoutUs = 0U;
  bool clockSetAfterBegin = false;
};

inline TwoWire Wire;
