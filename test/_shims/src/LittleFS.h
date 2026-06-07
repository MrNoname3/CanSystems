#pragma once
// Native-test shim for the LittleFS file system: an in-memory path -> content map.
// File is a small value-type handle (copyable, like the real fs::File) exposing both the
// Arduino raw read/write API (used by dataTransfer) and read()/readBytes() so ArduinoJson's
// default reader consumes it directly (deserializeJson(doc, file) in configHandler).
#include <stdint.h>
#include <string>
#include <map>
#include <cstddef>
#include <utility>

class File {
public:
  File() = default;

  // Read handle over a snapshot of the stored content.
  explicit File(std::string content) : buf_(std::move(content)), valid_(true) {}

  // Write handle: writes accumulate and are flushed to the store on close().
  File(std::map<std::string, std::string>* store, std::string path)
    : store_(store), path_(std::move(path)), valid_(true), write_(true) {}

  explicit operator bool() const { return valid_; }
  [[nodiscard]] size_t size() const { return buf_.size(); }
  [[nodiscard]] size_t available() const { return (pos_ < buf_.size()) ? (buf_.size() - pos_) : 0U; }

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static) mutates pos_; mirrors fs::File
  int read() {                                       // single byte, -1 at EOF (ArduinoJson default reader)
    if(pos_ >= buf_.size()) { return -1; }
    return static_cast<unsigned char>(buf_[pos_++]);
  }

  size_t readBytes(char* dst, size_t length) {       // ArduinoJson default reader
    size_t n = 0U;
    while((n < length) && (pos_ < buf_.size())) { dst[n++] = buf_[pos_++]; }
    return n;
  }

  size_t read(uint8_t* dst, size_t length) {         // Arduino raw read (dataTransfer)
    return readBytes(reinterpret_cast<char*>(dst), length);
  }

  size_t write(const uint8_t* src, size_t length) {  // Arduino raw write (dataTransfer)
    if(sWriteShouldFail) { return 0U; }              // injected disk-write failure
    buf_.append(reinterpret_cast<const char*>(src), length);
    return length;
  }

  static inline bool sWriteShouldFail = false;       // test hook: make write() report a short write

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static) flushes write buffer to the store
  void close() {
    if(write_ && (store_ != nullptr)) { (*store_)[path_] = buf_; }
  }

private:
  std::string buf_;
  std::map<std::string, std::string>* store_ = nullptr;
  std::string path_;
  size_t pos_   = 0U;
  bool   valid_ = false;
  bool   write_ = false;
};

class LittleFsShim {
public:
  [[nodiscard]] bool begin() const { return beginResult_; }

  // Hands out a mutable alias to files_, so it cannot be const/static despite clang-tidy's hints.
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static,readability-make-member-function-const)
  [[nodiscard]] File open(const char* path, const char* mode) {
    if((mode != nullptr) && (mode[0] == 'w')) {
      if(failWriteOpen_) { return {}; }          // injected open-for-write failure
      return File(&files_, path);
    }
    const auto it = files_.find(path);
    if(it == files_.end()) { return {}; }
    return File(it->second);
  }

  // Mutates files_; clang-tidy's const/static hints here are false positives.
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static,readability-make-member-function-const)
  bool rename(const char* from, const char* to) {
    if(failRename_) { return false; }            // injected rename failure
    const auto it = files_.find(from);
    const bool found = (it != files_.end());
    if(found) {
      files_[to] = it->second;
      files_.erase(it);
    }
    return found;
  }

  [[nodiscard]] size_t totalBytes() const { return capacity_; }
  [[nodiscard]] size_t usedBytes() const {
    size_t used = 0U;
    // cppcheck-suppress useStlAlgorithm
    for(const auto& entry : files_) { used += entry.second.size(); }
    return used;
  }

  // ---- test helpers ----
  void setFile(const char* path, const std::string& content) { files_[path] = content; }
  [[nodiscard]] bool exists(const char* path) const { return files_.count(path) > 0U; }
  [[nodiscard]] std::string fileContent(const char* path) const {
    const auto it = files_.find(path);
    return (it == files_.end()) ? std::string() : it->second;
  }
  void setBeginResult(bool result) { beginResult_ = result; }
  void setCapacity(size_t capacity) { capacity_ = capacity; }
  void setFailWriteOpen(bool fail) { failWriteOpen_ = fail; }
  void setFailWrite(bool fail) { File::sWriteShouldFail = fail; }      // NOLINT(readability-convert-member-functions-to-static)
  void setFailRename(bool fail) { failRename_ = fail; }
  void reset() {
    files_.clear();
    beginResult_ = true; failWriteOpen_ = false; failRename_ = false;
    File::sWriteShouldFail = false;
    capacity_ = defaultCapacity;
  }

private:
  static constexpr size_t defaultCapacity = 2024U * 1024U;  // mirrors the 2 MB LittleFS partition
  std::map<std::string, std::string> files_;
  bool   beginResult_ = true;
  bool   failWriteOpen_ = false;
  bool   failRename_ = false;
  size_t capacity_ = defaultCapacity;
};

inline LittleFsShim LittleFS;
