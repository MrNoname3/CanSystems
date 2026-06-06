#pragma once
// Native-test shim for the ESP firmware Updater (ESP8266 Updater.h / ESP32 Update.h). dataTransfer
// drives it for the firmware-OTA path; each step's result is test-controllable so failure branches
// can be exercised. No bytes are actually flashed.
#include <stdint.h>
#include <cstddef>

class UpdateShim {
public:
  bool begin(size_t size) { lastBeginSize_ = size; return beginResult_; }              // NOLINT(readability-make-member-function-const)
  bool setMD5(const char* md5) { (void)md5; return setMd5Result_; }                    // NOLINT(readability-make-member-function-const)
  size_t write(const uint8_t* data, size_t len) { (void)data; written_ += len; return writeSucceeds_ ? len : 0U; }
  bool end(bool evenIfRemaining = true) { (void)evenIfRemaining; return endResult_; }  // NOLINT(readability-make-member-function-const)

  // ---- test controls ----
  void reset() {
    beginResult_ = true; setMd5Result_ = true; writeSucceeds_ = true; endResult_ = true;
    written_ = 0U; lastBeginSize_ = 0U;
  }
  void setBeginResult(bool r)  { beginResult_ = r; }
  void setSetMd5Result(bool r) { setMd5Result_ = r; }
  void setWriteSucceeds(bool r){ writeSucceeds_ = r; }
  void setEndResult(bool r)    { endResult_ = r; }
  [[nodiscard]] size_t written() const { return written_; }
  [[nodiscard]] size_t lastBeginSize() const { return lastBeginSize_; }

private:
  bool   beginResult_   = true;
  bool   setMd5Result_  = true;
  bool   writeSucceeds_ = true;
  bool   endResult_     = true;
  size_t written_       = 0U;
  size_t lastBeginSize_ = 0U;
};

inline UpdateShim Update;
