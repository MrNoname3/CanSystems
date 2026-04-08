#if defined(ARDUINO_ARCH_ESP32)
#pragma once

#include "CANController.h"

class ESP32SJA1000 final : public CANController {
public:
  ESP32SJA1000() = default;
  ~ESP32SJA1000() override = default;

  [[nodiscard]] uint8_t begin(uint32_t baudRate) override;
  void end() override;

  [[nodiscard]] uint8_t endPacket() override;
  [[nodiscard]] uint8_t parsePacket() override;

  void onReceive(void(*callback)(int)) override;

  using CANController::filter;
  [[nodiscard]] uint8_t filter(uint16_t id, uint16_t mask) override;
  using CANController::filterExtended;
  [[nodiscard]] uint8_t filterExtended(uint32_t id, uint32_t mask) override;

  [[nodiscard]] uint8_t observe() override;
  [[nodiscard]] uint8_t loopback() override;
  [[nodiscard]] uint8_t sleep() override;
  [[nodiscard]] uint8_t wakeup() override;

  void setPins(int rx, int tx);

  void dumpRegisters(Stream& out);

private:
  static constexpr gpio_num_t defaultRxPin = GPIO_NUM_4;
  static constexpr gpio_num_t defaultTxPin = GPIO_NUM_5;

  void reset();
  void handleInterrupt();

  uint8_t readRegister(uint8_t address);
  void modifyRegister(uint8_t address, uint8_t mask, uint8_t value);
  void writeRegister(uint8_t address, uint8_t value);

  static void onInterrupt(void* arg);

  gpio_num_t rxPin       = defaultRxPin;
  gpio_num_t txPin       = defaultTxPin;
  bool loopbackEnabled   = false;
  intr_handle_t intrHandle = nullptr;
};

extern ESP32SJA1000 CAN;

#endif // ARDUINO_ARCH_ESP32
