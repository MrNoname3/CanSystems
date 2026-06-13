#pragma once
#ifdef ARDUINO_ARCH_AVR
#include "canHandlerBase.hpp"                                       /// Base class for CAN handling.
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "SPIFlash.h"                                               /// SPI FLASH module driver.
#include "ota.hpp"                                                  /// OTA (Over-The-Air) update handler.
#include "debugLedHandler.hpp"                                      /// Handles the debug LED.
#include "common.hpp"                                               /// Common definitions and functions.

/// @brief Handles CAN communication, OTA updates, and peripheral interactions.
class CanHandlerAtmega328P final : public CanHandlerBase {
private:
  static constexpr uint16_t pingTime = Time::secToMs(2U);           // Ping timeout in milliseconds.
  static constexpr uint16_t flashJedecId = 0xEF40U;                 // JEDEC ID for Winbond 64Mbit flash.

public:
  using CanHandlerBase::send;                                       // Bring the send methods of the base class into scope.

  /// @brief Constructor for the CAN handler.
  /// @param debugLed Reference to a `DebugLedHandler` object for debug feedback.
  /// @param canCsPin Chip select pin for the CAN module's SPI interface.
  /// @param canIntPin Interrupt pin for CAN frame notifications.
  /// @param flashCsPin Chip select pin for the SPI flash module.
  CanHandlerAtmega328P(DebugLedHandler& debugLed, uint8_t canCsPin, uint8_t canIntPin, uint8_t flashCsPin);

  /// @brief Default destructor.
  ~CanHandlerAtmega328P() override = default;

  /// @brief Initializes the CAN handler.
  /// @details Sets the CAN bus speed to 500 Kb/s.
  /// @return `true`.
  bool init() override {
    return init(500000U);
  }

  /// @brief Handles ongoing CAN communication in a non-blocking loop.
  /// @return `true` if successful, `false` otherwise.
  bool run() override;

  /// @brief Sends a CAN frame with a specified command and data payload.
  /// @param command 10-bit command value representing the specific action or request.
  /// @param data Array of 8 bytes containing the payload.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  bool send(uint16_t command, const uint8_t (&data)[8]) const override; // NOLINT(modernize-use-nodiscard)

  /// @brief Adds a custom callback for handling incoming CAN frames.
  /// @param canCallback Pointer to the callback function.
  inline void addCanCallback(void (*canCallback)(uint16_t command, const uint8_t (&data)[8])) {
    this->canCallback = canCallback;
  }

  CanHandlerAtmega328P(const CanHandlerAtmega328P&) = delete;                       // Define copy constructor.
  CanHandlerAtmega328P& operator=(const CanHandlerAtmega328P&) = delete;            // Define copy assignment operator.
  CanHandlerAtmega328P(CanHandlerAtmega328P&&) = delete;                            // Define move constructor.
  CanHandlerAtmega328P& operator=(CanHandlerAtmega328P&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Initializes the CAN bus with the specified baud rate.
  /// @param canBaud Baud rate for CAN communication.
  /// @return `true` if successful, `false` otherwise.
  [[nodiscard]] bool init(uint32_t canBaud);

  /// @brief Parses and dispatches a single received CAN frame.
  /// @return `true` if successful, `false` otherwise.
  bool handleRxFrame();

  /// @brief Interrupt handler for tracking received CAN frames.
  static inline void rxInterrupt() { intCount++; }

  /// @brief Sends the firmware version over CAN.
  /// @return `true` if successful, `false` otherwise.
  bool sendFwVersion() const; // NOLINT(modernize-use-nodiscard)

  static volatile uint8_t intCount;                                         // Interrupt counter for received CAN frames.

  DebugLedHandler& debugLed;                                                // Reference to debug LED handler object.
  SPIFlash flash;                                                           // SPI flash module driver object.
  OTA ota;                                                                  // OTA update handler.
  void (*canCallback)(uint16_t command, const uint8_t (&data)[8]);          // Callback function pointer.
  uint32_t eventTimer;                                                      // Class wide variable for universal timings.
  OTA::OtaState lastOtaState;                                               // Store last known OTA state.
};
using CanHandler = CanHandlerAtmega328P;                                    // Alias `CanHandler` to `CanHandlerAtmega328P`.
#endif // ARDUINO_ARCH_AVR