#pragma once
#include <stdint.h>
#include <deque>

class TwoWire {
public:
  void begin() {}
  void setClock(uint32_t /*hz*/) {}
  void setWireTimeout(uint32_t /*timeout*/, bool /*reset*/) {}
  void beginTransmission(uint8_t /*addr*/) {}

  [[nodiscard]] uint8_t endTransmission() const { return txResult; }

  uint8_t requestFrom(uint8_t /*addr*/, uint8_t n) {
    return static_cast<uint8_t>(readQueue.size() >= static_cast<size_t>(n) ? n : 0U);
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
  void reset() {
    readQueue.clear();
    txResult = 0U;
  }

private:
  std::deque<uint8_t> readQueue;
  uint8_t txResult = 0U;
};

inline TwoWire Wire;
