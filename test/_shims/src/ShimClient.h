#pragma once

#include "Arduino.h"
#include "Client.h"
#include "IPAddress.h"
#include "Buffer.h"

class ShimClient : public Client {
private:
  Buffer* responseBuffer;
  Buffer* expectBuffer;
  bool _allowConnect;
  bool _connected;
  bool expectAnything;
  bool _error;
  uint16_t _received;
  uint16_t _writesToFail;

  /// @brief Matches one written byte against what the test said to expect.
  void checkExpected(uint8_t actual);
  IPAddress _expectedIP;
  uint16_t _expectedPort;
  const char* _expectedHost;

public:
  ShimClient();
  ~ShimClient() override;                             // Frees responseBuffer/expectBuffer allocated in the constructor.
  ShimClient(const ShimClient&) = delete;
  ShimClient& operator=(const ShimClient&) = delete;
  bool connect(IPAddress ip, uint16_t port) override;
  bool connect(const char* host, uint16_t port) override;
  size_t write(uint8_t) override;
  size_t write(const uint8_t* buf, size_t size) override;
  int available() override;
  int read() override;
  int read(uint8_t* buf, size_t size) override;
  int peek() override;
  void flush() override;
  void stop() override;
  uint8_t connected() override;
  operator bool() override;

  ShimClient* respond(const uint8_t* buf, size_t size);
  ShimClient* expect(const uint8_t* buf, size_t size);

  void expectConnect(IPAddress ip, uint16_t port);
  void expectConnect(const char* host, uint16_t port);

  [[nodiscard]] uint16_t received() const;
  [[nodiscard]] bool error() const;

  void setAllowConnect(bool b);
  void setConnected(bool b);

  /// @brief Makes the next `count` writes report that nothing was sent.
  /// @details Models what the secure client does when its engine cannot take application data:
  /// it returns 0 rather than failing outright. A refused write consumes nothing, so the bytes
  /// the caller retries are still matched against `expect()`.
  void failNextWrites(uint16_t count);
};
