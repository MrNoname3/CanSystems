#pragma once

#include <Arduino.h>

/// @brief Base class holding the packet state a CAN controller driver builds on.
/// @details The CAN operations are deliberately non-virtual: CAN.h picks one implementation at
/// compile time, the controller object is a concrete global, and no caller reaches a driver
/// through a base pointer. Virtual dispatch would only pin every method into the vtable, where
/// the linker cannot drop the ones a firmware never calls.
class CANController {
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

  /// @brief Appends one byte to the packet being built.
  /// @return 1 when it fit, 0 when no packet is open or the payload is already full.
  size_t write(uint8_t b);

  /// @brief Appends bytes to the packet being built.
  /// @return How many bytes were appended; less than `size` when the payload filled up.
  size_t write(const uint8_t* buffer, size_t size);

  /// @brief Bytes of the received packet not read yet.
  [[nodiscard]] int available() const;

  /// @brief Takes the next byte of the received packet, or -1 when there is none.
  int read();

  /// @brief Returns the next byte of the received packet without taking it, or -1.
  [[nodiscard]] int peek() const;

  /// @brief Copies up to `length` unread bytes of the received packet into `buffer`.
  /// @return How many bytes were copied; less than `length` when the packet holds fewer.
  /// @note Returns what is already buffered and never waits, so it is safe to call from an
  /// interrupt.
  size_t readBytes(uint8_t* buffer, size_t length);

  /// @brief Set the receive interrupt callback.
  void onReceive(void (*callback)(int));

protected:
  CANController();
  ~CANController() = default;

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
