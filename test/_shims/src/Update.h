#pragma once
// Native-test shim for the ESP firmware Updater (ESP8266 Updater.h / ESP32 Update.h), modelled on
// the real class so dataTransfer's firmware path is exercised faithfully: begin() rejects a zero
// size and refuses while an image is still open, setMD5() requires a 32-char hash, write() refuses
// to overrun the announced size, and end() verifies that exactly `size` bytes arrived and their
// MD5 matches setMD5(). An image that does not finish is only released by abort() or by end()
// rejecting it, which is what makes a missing cleanup visible here. Each step also has a test
// override so the failure branches can be forced. No bytes are actually flashed.
#include "MD5Builder.h"
#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <cctype>
#include <string>

class UpdateShim {
public:
  bool begin(size_t size) {
    if((size_ > 0U) || (size == 0U) || !beginResult_) { return false; }   // "already running" in both real classes
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
    totalWritten_ += len;
    return len;
  }

  bool end(bool evenIfRemaining = false) {
    (void)evenIfRemaining;
    if(size_ == 0U) { return false; }
    if(!endResult_ || (progress_ != size_)) {                // premature: not all bytes received
      release();                                             // both real classes reset here
      return false;
    }
    if(!target_.empty()) {
      md5_.calculate();
      if(!equalsIgnoreCase(md5_.toString(), target_)) {
        release();
        return false;
      }
    }
    release();                                               // ready for the next begin(), like the real class
    return true;
  }

  // ESP32 only; the ESP8266 class has no abort() and dataTransfer reaches end(false) there.
  void abort() { release(); }

  // ---- test controls ----
  void reset() {
    beginResult_ = true;
    setMd5Result_ = true;
    writeSucceeds_ = true;
    endResult_ = true;
    size_ = 0U;
    progress_ = 0U;
    totalWritten_ = 0U;
    target_.clear();
  }
  void setBeginResult(bool r) { beginResult_ = r; }
  void setSetMd5Result(bool r) { setMd5Result_ = r; }
  void setWriteSucceeds(bool r) { writeSucceeds_ = r; }
  void setEndResult(bool r) { endResult_ = r; }
  [[nodiscard]] size_t written() const { return totalWritten_; }   // cumulative, survives a release
  [[nodiscard]] bool isOpen() const { return size_ > 0U; }          // models the real _size > 0

private:
  void release() {
    size_ = 0U;
    progress_ = 0U;
    target_.clear();
  }

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
  size_t totalWritten_ = 0U;                                        // Test observation only; release() leaves it alone.
  std::string target_;
  bool beginResult_ = true;
  bool setMd5Result_ = true;
  bool writeSucceeds_ = true;
  bool endResult_ = true;
};

inline UpdateShim Update;
