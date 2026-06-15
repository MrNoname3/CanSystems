#ifndef RESET_HANDLER_HPP
#define RESET_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Class for handling system resets.
class ResetHandler final {
public:
  /// @brief Resets the microcontroller unit (MCU) by triggering a watchdog reset.
  static void restartMCU();

#if defined(ESP8266) || defined(ESP32)
  /// @brief Retrieves the reason for the last system reset.
  /// @return A `uint8_t` value representing the reset reason, where each value corresponds to a specific reset cause.
  [[nodiscard]] static uint8_t getResetReason();

  /// @brief Returns true if the last reset was caused by any watchdog timer.
  [[nodiscard]] static bool isWdtReset();
#endif

  ResetHandler() = delete;                                           // Delete constructor.
  ~ResetHandler() = delete;                                          // Delete destructor.
  ResetHandler(const ResetHandler&) = delete;                       // Define copy constructor.
  ResetHandler& operator=(const ResetHandler&) = delete;            // Define copy assignment operator.
  ResetHandler(ResetHandler&&) = delete;                            // Define move constructor.
  ResetHandler& operator=(ResetHandler&&) = delete;                 // Define move assignment operator.
};
#endif // RESET_HANDLER_HPP
