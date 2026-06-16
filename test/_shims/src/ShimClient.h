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
  int16_t available() override;
  int16_t read() override;
  int16_t read(uint8_t* buf, size_t size) override;
  int16_t peek() override;
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
};
