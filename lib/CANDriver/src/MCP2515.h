#if !defined(ARDUINO_ARCH_ESP32)
#pragma once

#include <SPI.h>                                                    /// SPI bus the controller is reached over.
#include "CANController.h"                                          /// Packet state the driver builds on.

class MCP2515 final : public CANController {
public:
  MCP2515() = default;
  ~MCP2515() = default;

  /// @brief Resets the controller and puts it on the bus at the given baud rate.
  /// @param baudRate Bus bit rate; must be one of the rates the driver's table holds.
  /// @return 1 on success, 0 when the controller does not answer or the rate is unknown.
  [[nodiscard]] uint8_t begin(uint32_t baudRate);

  /// @brief Takes the controller off the bus and releases the SPI bus.
  void end();

  /// @brief Queues the packet being built in a transmit buffer.
  /// @return 1 once the frame is queued, 0 when no buffer could be freed in time.
  /// @note Returning 1 means the controller accepted the frame, not that the bus carried it;
  /// use flushTx() where that distinction matters.
  [[nodiscard]] uint8_t endPacket();

  /// @brief Reads the next received frame out of the controller, if there is one.
  /// @return The frame's payload length in bytes, 0 when no frame was waiting. A zero-length
  /// frame is reported through packetId() rather than this return value.
  [[nodiscard]] uint8_t parsePacket();

  /// @brief Set the receive interrupt callback.
  /// @note Needs an interrupt pin from setPins(); the callback runs in interrupt context and
  /// reaches the controller over SPI.
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

  /// @brief Waits until no transmit buffer holds a queued frame any more.
  /// @return `false` on timeout, after aborting the frames that were still stuck.
  [[nodiscard]] bool flushTx() const; // NOLINT(readability-convert-member-functions-to-static)

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

#if defined(ARDUINO_ARCH_SAMD) && defined(PIN_SPI_MISO) && defined(PIN_SPI_MOSI) && defined(PIN_SPI_SCK) && (PIN_SPI_MISO == 10) && (PIN_SPI_MOSI == 8) && (PIN_SPI_SCK == 9)
  static constexpr uint8_t defaultCsPin = 3U;                        // Chip select.
  static constexpr uint8_t defaultIntPin = 7U;                       // INT line, held low while a frame waits.
#else
  static constexpr uint8_t defaultCsPin = 10U;                       // Chip select.
  static constexpr uint8_t defaultIntPin = 2U;                       // INT line, held low while a frame waits.
#endif

  /// @brief Selects the pins the controller is wired to.
  /// @param cs Chip select pin.
  /// @param irq Interrupt pin; 0xFF leaves onReceive() without a line to attach to.
  /// @note Must be called before begin().
  void setPins(uint8_t cs = defaultCsPin, uint8_t irq = defaultIntPin);

  /// @brief Sets the SPI clock the driver talks to the controller at.
  /// @param frequency SPI clock in Hz.
  /// @note Must be called before begin().
  void setSPIFrequency(uint32_t frequency);

  /// @brief Tells the driver which crystal the controller runs on, so it can pick bit timings.
  /// @param freq Crystal frequency in Hz; the timing table covers 8 MHz and 16 MHz.
  /// @note Must be called before begin().
  void setClockFrequency(uint32_t freq);

  /// @brief Prints every controller register, for debugging.
  /// @param out Stream the dump is written to.
  void dumpRegisters(Stream& out); // NOLINT(readability-convert-member-functions-to-static)

private:
  static constexpr uint32_t defaultClockFrequency = 16'000'000U;
  static constexpr uint8_t txBufferCount = 3U;                      // TXB0..TXB2.
  // Backstop for a bus that never lets a queued frame out, not a per-frame budget: the
  // controller retransmits on its own, so a healthy bus never reaches this. Three frames take
  // about a millisecond at 500 kbit/s, which is the rate this driver is used at.
  static constexpr uint32_t txDrainTimeoutMs = 20U;

  /// @brief Reserves the transmit buffer the next frame belongs in.
  /// @return Buffer index, or `txBufferCount` when no buffer could be freed.
  [[nodiscard]] uint8_t takeTxBuffer();

  void reset() const; // NOLINT(readability-convert-member-functions-to-static)
  void handleInterrupt();

  [[nodiscard]] uint8_t readRegister(uint8_t address) const;
  void readBurst(uint8_t address, uint8_t* data, uint8_t length) const; // NOLINT(readability-convert-member-functions-to-static)
  void modifyRegister(uint8_t address, uint8_t mask, uint8_t value) const;
  void writeRegister(uint8_t address, uint8_t value) const;
  void writeBurst(uint8_t address, const uint8_t* data, uint8_t length) const; // NOLINT(readability-convert-member-functions-to-static)

  static void onInterrupt();

  SPISettings spiSettings = SPISettings(10E6, MSBFIRST, SPI_MODE0);
  uint8_t csPin = defaultCsPin;
  uint8_t intPin = defaultIntPin;
  uint32_t clockFrequency = defaultClockFrequency;
  uint8_t txBuffersLeft = txBufferCount;
};

extern MCP2515 CAN;

#endif // !ARDUINO_ARCH_ESP32
