#if defined(ARDUINO_ARCH_ESP32) || defined(NATIVE_TEST)
#pragma once

#include "CANController.h"
#include "driver/gpio.h"                                          /// gpio_num_t for the pin members.
#include "esp_intr_alloc.h"                                        /// intr_handle_t for the interrupt handle.

class ESP32SJA1000 final : public CANController {
public:
  ESP32SJA1000() = default;
  ~ESP32SJA1000() = default;

  [[nodiscard]] uint8_t begin(uint32_t baudRate);
  void end();

  [[nodiscard]] uint8_t endPacket();
  [[nodiscard]] uint8_t parsePacket();

  void onReceive(void (*callback)(int));

  [[nodiscard]] uint8_t filter(uint16_t id, uint16_t mask);
  [[nodiscard]] uint8_t filterExtended(uint32_t id, uint32_t mask);

  [[nodiscard]] uint8_t observe();
  [[nodiscard]] uint8_t loopback();
  [[nodiscard]] uint8_t sleep();
  [[nodiscard]] uint8_t wakeup();

  /// @brief Reports whether the controller is bus-off and therefore no longer on the bus.
  [[nodiscard]] bool isBusOff() const;

  /// @brief Brings the controller back after it dropped into bus-off.
  /// @details Clearing the reset request is what starts the protocol's recovery wait; the
  /// controller stays off the bus until it happens.
  void recoverFromBusOff();

  void setPins(uint8_t rx = static_cast<uint8_t>(defaultRxPin), uint8_t tx = static_cast<uint8_t>(defaultTxPin));

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
