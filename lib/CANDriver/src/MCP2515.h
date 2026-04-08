#if !defined(ARDUINO_ARCH_ESP32)
#pragma once

#include <SPI.h>
#include "CANController.h"

class MCP2515 final : public CANController {
public:
  MCP2515() = default;
  ~MCP2515() override = default;

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

#if defined(ARDUINO_ARCH_SAMD) && defined(PIN_SPI_MISO) && defined(PIN_SPI_MOSI) && defined(PIN_SPI_SCK) && (PIN_SPI_MISO == 10) && (PIN_SPI_MOSI == 8) && (PIN_SPI_SCK == 9)
  static constexpr uint8_t defaultCsPin  = 3U;
  static constexpr uint8_t defaultIntPin = 7U;
#else
  static constexpr uint8_t defaultCsPin  = 10U;
  static constexpr uint8_t defaultIntPin = 2U;
#endif

  void setPins(uint8_t cs = defaultCsPin, uint8_t irq = defaultIntPin);
  void setSPIFrequency(uint32_t frequency);
  void setClockFrequency(uint32_t freq);

  void dumpRegisters(Stream& out); // NOLINT(readability-convert-member-functions-to-static)

private:
  static constexpr uint32_t defaultClockFrequency = 16'000'000U;

  void reset(); // NOLINT(readability-convert-member-functions-to-static)
  void handleInterrupt();

  uint8_t readRegister(uint8_t address);
  void modifyRegister(uint8_t address, uint8_t mask, uint8_t value);
  void writeRegister(uint8_t address, uint8_t value);

  static void onInterrupt();

  SPISettings spiSettings = SPISettings(10E6, MSBFIRST, SPI_MODE0);
  uint8_t csPin           = defaultCsPin;
  uint8_t intPin          = defaultIntPin;
  uint32_t clockFrequency = defaultClockFrequency;
};

extern MCP2515 CAN;

#endif // !ARDUINO_ARCH_ESP32
