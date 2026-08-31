#if defined(ARDUINO_ARCH_ESP32) || defined(NATIVE_TEST)
#pragma once

#include "CANController.h"                                        /// Packet state the driver builds on.
#include "driver/gpio.h"                                          /// gpio_num_t for the pin members.
#include "esp_intr_alloc.h"                                        /// intr_handle_t for the interrupt handle.

class ESP32SJA1000 final : public CANController {
public:
  ESP32SJA1000() = default;
  ~ESP32SJA1000() = default;

  /// @brief Resets the controller and puts it on the bus at the given baud rate.
  /// @param baudRate Bus bit rate; must be one of the rates the driver's table holds.
  /// @return 1 on success, 0 when the rate is unknown.
  [[nodiscard]] uint8_t begin(uint32_t baudRate);

  /// @brief Takes the controller off the bus and releases its pins and interrupt.
  void end();

  /// @brief Sends the packet being built and waits for the controller to finish with it.
  /// @return 1 once the frame has gone out, 0 on a transmission error, on bus-off, or when the
  /// wait ran past its timeout.
  [[nodiscard]] uint8_t endPacket();

  /// @brief Reads the next received frame out of the controller, if there is one.
  /// @return The frame's payload length in bytes, 0 when no frame was waiting. A zero-length
  /// frame is reported through packetId() rather than this return value.
  [[nodiscard]] uint8_t parsePacket();

  /// @brief Set the receive interrupt callback.
  /// @note The callback runs in interrupt context.
  void onReceive(void (*callback)(int));

  /// @brief Installs an acceptance filter for standard 11-bit identifiers.
  /// @param id Identifier the filter accepts.
  /// @param mask Bits of `id` that have to match.
  /// @return 1 on success, 0 when the controller did not take the configuration.
  [[nodiscard]] uint8_t filter(uint16_t id, uint16_t mask);

  /// @brief Installs an acceptance filter for extended 29-bit identifiers.
  /// @param id Identifier the filter accepts.
  /// @param mask Bits of `id` that have to match.
  /// @return 1 on success, 0 when the controller did not take the configuration.
  [[nodiscard]] uint8_t filterExtended(uint32_t id, uint32_t mask);

  /// @brief Switches to listen-only mode: frames are received but never acknowledged.
  /// @return 1 on success, 0 when the mode did not take.
  [[nodiscard]] uint8_t observe();

  /// @brief Switches to loopback mode, where sent frames come back without reaching the bus.
  /// @return 1 on success, 0 when the mode did not take.
  [[nodiscard]] uint8_t loopback();

  /// @brief Puts the controller to sleep.
  /// @return 1 on success, 0 when the mode did not take.
  [[nodiscard]] uint8_t sleep();

  /// @brief Returns the controller to normal mode from sleep, loopback or listen-only.
  /// @return 1 on success, 0 when the mode did not take.
  [[nodiscard]] uint8_t wakeup();

  /// @brief Reports whether the controller is bus-off and therefore no longer on the bus.
  [[nodiscard]] bool isBusOff() const;

  /// @brief Brings the controller back after it dropped into bus-off.
  /// @details Clearing the reset request is what starts the protocol's recovery wait; the
  /// controller stays off the bus until it happens.
  void recoverFromBusOff();

  /// @brief Selects the pins the transceiver is wired to.
  /// @param rx Receive pin.
  /// @param tx Transmit pin.
  /// @note Must be called before begin().
  void setPins(uint8_t rx = static_cast<uint8_t>(defaultRxPin), uint8_t tx = static_cast<uint8_t>(defaultTxPin));

  /// @brief Prints every controller register, for debugging.
  /// @param out Stream the dump is written to.
  static void dumpRegisters(Stream& out);

private:
  static constexpr gpio_num_t defaultRxPin = GPIO_NUM_4;
  static constexpr gpio_num_t defaultTxPin = GPIO_NUM_5;

  void reset();
  void handleInterrupt();

  static uint8_t readRegister(uint8_t address);
  static void modifyRegister(uint8_t address, uint8_t mask, uint8_t value);
  static void writeRegister(uint8_t address, uint8_t value);

  static void onInterrupt(void* arg);

  gpio_num_t rxPin = defaultRxPin;
  gpio_num_t txPin = defaultTxPin;
  bool loopbackEnabled = false;
  intr_handle_t intrHandle = nullptr;
};

#if !defined(NATIVE_TEST)
extern ESP32SJA1000 CAN;      // On the host the tests construct their own; CAN is the MCP2515 there.
#endif

#endif // ARDUINO_ARCH_ESP32 || NATIVE_TEST
