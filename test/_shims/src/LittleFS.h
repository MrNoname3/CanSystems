#pragma once
// Minimal native-test shim for the LittleFS file system, just enough for configHandler:
// an in-memory path -> content map served as read-only File streams. File derives from
// std::istream so ArduinoJson's StdStreamReader consumes it directly (deserializeJson(doc, file)).
#include <istream>
#include <sstream>
#include <string>
#include <map>
#include <cstddef>

class File : public std::istream {
public:
  File() = default;
  explicit File(const std::string& content)
    : std::istream(nullptr), buf_(content, std::ios_base::in), size_(content.size()), valid_(true) {
    this->rdbuf(&buf_);
  }
  explicit operator bool() const { return valid_; }
  [[nodiscard]] size_t size() const { return size_; }
  void close() {}

  File(const File&) = delete;
  File& operator=(const File&) = delete;
  File(File&&) = delete;
  File& operator=(File&&) = delete;

private:
  std::stringbuf buf_;
  size_t size_ = 0U;
  bool   valid_ = false;
};

class LittleFsShim {
public:
  [[nodiscard]] bool begin() const { return beginResult_; }

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static) accesses files_ on real targets
  [[nodiscard]] File open(const char* path, const char* /*mode*/) const {
    const auto it = files_.find(path);
    if(it == files_.end()) { return {}; }
    return File(it->second);
  }

  // ---- test helpers ----
  void setFile(const char* path, const std::string& content) { files_[path] = content; }
  void setBeginResult(bool result) { beginResult_ = result; }
  void reset() { files_.clear(); beginResult_ = true; }

private:
  std::map<std::string, std::string> files_;
  bool beginResult_ = true;
};

inline LittleFsShim LittleFS;
