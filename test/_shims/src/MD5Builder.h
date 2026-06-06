#pragma once
// Native-test shim for the Arduino MD5Builder. dataTransfer only compares the produced digest
// string against the expected MD5, so the shim returns a test-settable result instead of a real
// hash; this exercises dataTransfer's match/mismatch branches without depending on an MD5 impl.
#include <stdint.h>
#include <string>

namespace md5shim {
  inline std::string forcedResult;                         // digest returned by toString()
  inline void setResult(const std::string& result) { forcedResult = result; }
}

class MD5Builder {
public:
  void begin() {}                                          // NOLINT(readability-convert-member-functions-to-static)
  void add(const uint8_t* data, uint16_t len) { (void)data; addedBytes_ += len; }
  void calculate() {}                                      // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] std::string toString() const { return md5shim::forcedResult; }  // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] uint32_t addedBytes() const { return addedBytes_; }

private:
  uint32_t addedBytes_ = 0U;
};
