#pragma once

#include "Arduino.h"
#include <vector>

class Buffer;   // Only a pointer is held here; Stream.cpp includes the full Buffer.h.

class Stream {
private:
  Buffer* expectBuffer;
  bool _error;
  uint16_t _written;

public:
  Stream();
  ~Stream();                                         // Frees the heap-allocated expectBuffer (defined in Stream.cpp where Buffer is complete).
  Stream(const Stream&) = delete;
  Stream& operator=(const Stream&) = delete;
  size_t write(uint8_t);

  // RX stubs for write-only consumers (DFPlayerMiniFast::flush/query); no simulated input.
  // Defined in Stream.cpp so static analysis of the consumers cannot fold them to constants.
  [[nodiscard]] int available() const;
  int read();

  [[nodiscard]] bool error() const;
  void expect(const uint8_t* buf, size_t size);
  [[nodiscard]] uint16_t length() const;

  // Every byte written through any Stream, for packet-level test assertions (dfPlayer).
  static inline std::vector<uint8_t> captured;
  static void clearCaptured() { captured.clear(); }
};
