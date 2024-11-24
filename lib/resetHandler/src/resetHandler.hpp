#ifndef RESET_HANDLER_HPP
#define RESET_HANDLER_HPP

#include <Arduino.h>                                                /// Arduino libraries header.
#include <avr/wdt.h>                                                /// Watchdog timer library.

/// @brief Class for handling system resets.
class ResetHandler final {
public:
  /// @brief Default constructor.
  ResetHandler() = default;

  /// @brief Default destructor.
  ~ResetHandler() = default;

  /// @brief Resets the microcontroller unit (MCU) by triggering a watchdog reset.
  static inline void restartMCU() {
    Serial.println(F("Restarting..."));                 // Logs a restart message to the serial monitor.
    Serial.flush();                                     // Sends out data from the serial buffer before reset.
    wdt_enable(WDTO_15MS);                              // Configures the watchdog timer for a 15-ms timeout.
    while(true) {};                                     // Triggers a reset by waiting indefinitely.
  }

  ResetHandler(const ResetHandler&) = delete;                       // Define copy constructor.
  ResetHandler& operator=(const ResetHandler&) = delete;            // Define copy assignment operator.
  ResetHandler(ResetHandler&&) = delete;                            // Define move constructor.
  ResetHandler& operator=(ResetHandler&&) = delete;                 // Define move assignment operator.
};
#endif // RESET_HANDLER_HPP