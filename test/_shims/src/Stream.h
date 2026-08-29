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

  // RX side. Virtual and non-const to match the Arduino Stream, so a subclass that serves its
  // own buffer (CANController) really overrides them and readBytes() reaches its data.
  // Defined in Stream.cpp so static analysis of the consumers cannot fold them to constants.
  virtual int available();
  virtual int read();
  virtual int peek();
  virtual void flush();
  size_t readBytes(uint8_t* buffer, size_t length);
  void setTimeout(unsigned long timeout);

  // Generic no-op print/println accepting any Arduino overload, mirroring HardwareSerial's
  // stubs. Real Streams inherit these from Print; consumers only need them to compile.
  template<typename... Args>
  size_t print(Args... /*args*/) { return 0U; }      // NOLINT(readability-convert-member-functions-to-static)
  template<typename... Args>
  size_t println(Args... /*args*/) { return 0U; }    // NOLINT(readability-convert-member-functions-to-static)

  [[nodiscard]] bool error() const;
  void expect(const uint8_t* buf, size_t size);
  [[nodiscard]] uint16_t length() const;

  // Every byte written through any Stream, for packet-level test assertions (dfPlayer).
  static inline std::vector<uint8_t> captured;
  static void clearCaptured() { captured.clear(); }
};
