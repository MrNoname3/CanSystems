#pragma once

#include <Arduino.h>

/// @brief Abstract base class for CAN controllers.
class CANController : public Stream {
public:
  /// @brief Initialize the CAN controller at the given baud rate.
  /// @return 1 on success, 0 on failure.
  [[nodiscard]] virtual uint8_t begin(uint32_t baudRate);

  /// @brief Deinitialize the CAN controller.
  virtual void end();

  /// @brief Begin a standard 11-bit CAN packet.
  /// @return 1 on success, 0 on failure.
  [[nodiscard]] uint8_t beginPacket(uint16_t id, uint8_t dlc, bool rtr = false);

  /// @brief Begin an extended 29-bit CAN packet.
  /// @return 1 on success, 0 on failure.
  [[nodiscard]] uint8_t beginExtendedPacket(uint32_t id, uint8_t dlc, bool rtr = false);

  /// @brief Finalize and transmit the current CAN packet.
  /// @return 1 on success, 0 on failure.
  [[nodiscard]] virtual uint8_t endPacket();

  /// @brief Parse the next received CAN packet.
  /// @return DLC (0..8) on success, 0 if no packet.
  [[nodiscard]] virtual uint8_t parsePacket();

  /// @brief Return the ID of the last received packet.
  [[nodiscard]] uint32_t packetId() const;

  /// @brief Return whether the last received packet was extended (29-bit).
  [[nodiscard]] bool packetExtended() const;

  /// @brief Return whether the last received packet was a remote transmission request.
  [[nodiscard]] bool packetRtr() const;

  /// @brief Return the DLC of the last received packet.
  [[nodiscard]] uint8_t packetDlc() const;

  // from Print
  virtual size_t write(uint8_t b);
  virtual size_t write(const uint8_t* buffer, size_t size);

  // from Stream
  virtual int available();
  virtual int read();
  virtual int peek();
  virtual void flush();

  /// @brief Set the receive interrupt callback.
  virtual void onReceive(void(*callback)(int));

  [[nodiscard]] virtual uint8_t filter(uint16_t id) { return filter(id, 0x7FFU); }
  [[nodiscard]] virtual uint8_t filter(uint16_t id, uint16_t mask);
  [[nodiscard]] virtual uint8_t filterExtended(uint32_t id) { return filterExtended(id, 0x1FFFFFFFU); }
  [[nodiscard]] virtual uint8_t filterExtended(uint32_t id, uint32_t mask);

  [[nodiscard]] virtual uint8_t observe();
  [[nodiscard]] virtual uint8_t loopback();
  [[nodiscard]] virtual uint8_t sleep();
  [[nodiscard]] virtual uint8_t wakeup();

protected:
  CANController();
  virtual ~CANController() = default;

  void (*onReceiveCb)(int);

  bool packetBegun;
  static constexpr uint32_t noId = UINT32_MAX;

  uint32_t txId;
  bool txExtended;
  bool txRtr;
  uint8_t txDlc;
  uint8_t txLength;
  uint8_t txData[8];

  uint32_t rxId;
  bool rxExtended;
  bool rxRtr;
  uint8_t rxDlc;
  uint8_t rxLength;
  uint8_t rxIndex;
  uint8_t rxData[8];
};
