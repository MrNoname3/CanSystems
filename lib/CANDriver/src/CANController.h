#pragma once

#include <Arduino.h>

/// @brief Base class holding the packet state a CAN controller driver builds on.
/// @details The CAN operations are deliberately non-virtual. CAN.h picks one implementation at
/// compile time, the controller object is a concrete global, and nothing ever reaches a driver
/// through a base pointer - so virtual dispatch bought nothing while pinning every method into
/// the vtable, where the linker cannot drop the ones a firmware never calls. Only what Stream
/// and Print require stays virtual.
class CANController : public Stream {
public:
  /// @brief Initialize the CAN controller at the given baud rate.
  /// @return 1 on success, 0 on failure.
  [[nodiscard]] uint8_t begin(uint32_t baudRate);

  /// @brief Deinitialize the CAN controller.
  void end();

  /// @brief Begin a standard 11-bit CAN packet.
  /// @return 1 on success, 0 on failure.
  [[nodiscard]] uint8_t beginPacket(uint16_t id, uint8_t dlc, bool rtr = false);

  /// @brief Begin an extended 29-bit CAN packet.
  /// @return 1 on success, 0 on failure.
  [[nodiscard]] uint8_t beginExtendedPacket(uint32_t id, uint8_t dlc, bool rtr = false);

  /// @brief Finalize and transmit the current CAN packet.
  /// @return 1 on success, 0 on failure.
  [[nodiscard]] uint8_t endPacket();

  /// @brief Identifier reported by packetId() when no packet was received.
  static constexpr uint32_t noId = UINT32_MAX;

  /// @brief Return the ID of the last received packet, or `noId` if there was none.
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
  int available() override;
  int read() override;
  int peek() override;
  void flush() override;

  /// @brief Set the receive interrupt callback.
  void onReceive(void (*callback)(int));

protected:
  CANController();
  virtual ~CANController() = default;

  void (*onReceiveCb)(int);

  bool packetBegun;

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
