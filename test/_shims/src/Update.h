#pragma once
// Native-test shim for the ESP firmware Updater (ESP8266 Updater.h / ESP32 Update.h), modelled on
// the real class so dataTransfer's firmware path is exercised faithfully: begin() rejects a zero
// size, setMD5() requires a 32-char hash, write() refuses to overrun the announced size, and end()
// verifies that exactly `size` bytes arrived and their MD5 matches setMD5(). Each step also has a
// test override so the failure branches can be forced. No bytes are actually flashed.
#include "MD5Builder.h"
#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <cctype>
#include <string>

class UpdateShim {
public:
  bool begin(size_t size) {
    if((size == 0U) || !beginResult_) { return false; }
    size_ = size;
    progress_ = 0U;
    target_.clear();
    md5_.begin();
    return true;
  }

  bool setMD5(const char* md5) {                              // NOLINT(readability-make-member-function-const) stores the target hash
    if((md5 == nullptr) || (std::strlen(md5) != 32U)) { return false; }
    target_ = md5;
    return setMd5Result_;
  }

  size_t write(const uint8_t* data, size_t len) {
    if(!writeSucceeds_ || ((progress_ + len) > size_)) { return 0U; }
    md5_.add(data, static_cast<uint16_t>(len));
    progress_ += len;
    return len;
  }

  bool end(bool evenIfRemaining = true) {
    (void)evenIfRemaining;
    if((size_ == 0U) || !endResult_) { return false; }
    if(progress_ != size_) { return false; }                 // premature: not all bytes received
    if(!target_.empty()) {
      md5_.calculate();
      if(!equalsIgnoreCase(md5_.toString(), target_)) { return false; }
    }
    size_ = 0U;                                              // ready for the next begin(), like the real class
    return true;
  }

  // ---- test controls ----
  void reset() {
    beginResult_ = true;
    setMd5Result_ = true;
    writeSucceeds_ = true;
    endResult_ = true;
    size_ = 0U;
    progress_ = 0U;
    target_.clear();
  }
  void setBeginResult(bool r) { beginResult_ = r; }
  void setSetMd5Result(bool r) { setMd5Result_ = r; }
  void setWriteSucceeds(bool r) { writeSucceeds_ = r; }
  void setEndResult(bool r) { endResult_ = r; }
  [[nodiscard]] size_t written() const { return progress_; }

private:
  static bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if(a.size() != b.size()) { return false; }
    for(size_t i = 0U; i < a.size(); ++i) {
      if(std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
        return false;
      }
    }
    return true;
  }

  MD5Builder md5_;
  size_t size_ = 0U;
  size_t progress_ = 0U;
  std::string target_;
  bool beginResult_ = true;
  bool setMd5Result_ = true;
  bool writeSucceeds_ = true;
  bool endResult_ = true;
};

inline UpdateShim Update;
