#ifndef RESET_HANDLER_HPP
#define RESET_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <HardwareSerial.h>                                         /// Hardware serial driver for communication with peripheral devices.

/// @brief Class for handling system resets.
class ResetHandler final {
private:
  /// @brief Delete constructor.
  ResetHandler() = delete;

  /// @brief Delete destructor.
  ~ResetHandler() = delete;

public:
  /// @brief Resets the microcontroller unit (MCU) by triggering a watchdog reset.
  static void restartMCU(HardwareSerial& serial = Serial);

#if defined(ESP8266) || defined(ESP32)
  /// @brief Retrieves the reason for the last system reset.
  /// @return A `uint8_t` value representing the reset reason, where each value corresponds to a specific reset cause.
  static uint8_t getResetReason();
#endif

  ResetHandler(const ResetHandler&) = delete;                       // Define copy constructor.
  ResetHandler& operator=(const ResetHandler&) = delete;            // Define copy assignment operator.
  ResetHandler(ResetHandler&&) = delete;                            // Define move constructor.
  ResetHandler& operator=(ResetHandler&&) = delete;                 // Define move assignment operator.
};
#endif // RESET_HANDLER_HPP