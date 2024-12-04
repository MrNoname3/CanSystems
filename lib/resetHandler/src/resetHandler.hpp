#ifndef RESET_HANDLER_HPP
#define RESET_HANDLER_HPP

#include <Arduino.h>                                                /// Arduino libraries header.
#if defined(__AVR_ATmega328P__)
#include <avr/wdt.h>                                                /// Watchdog timer library.
#endif

/// @brief Class for handling system resets.
class ResetHandler final {
private:
  /// @brief Delete constructor.
  ResetHandler() = delete;

  /// @brief Delete destructor.
  ~ResetHandler() = delete;

public:
  /// @brief Resets the microcontroller unit (MCU) by triggering a watchdog reset.
  static inline void restartMCU() {
#if defined(__AVR_ATmega328P__)
    Serial.println(F("Restarting..."));
    Serial.flush();                                     // Sends out data from the serial buffer before reset.
    wdt_enable(WDTO_15MS);                              // Configures the watchdog timer for a 15-ms timeout.
#elif defined(ESP32) || defined(ESP8266)
    Serial.printf_P(PSTR("Restarting...\r\n"));
    Serial.flush();
    ESP.restart();
#endif
    while(true) {};
  }

  ResetHandler(const ResetHandler&) = delete;                       // Define copy constructor.
  ResetHandler& operator=(const ResetHandler&) = delete;            // Define copy assignment operator.
  ResetHandler(ResetHandler&&) = delete;                            // Define move constructor.
  ResetHandler& operator=(ResetHandler&&) = delete;                 // Define move assignment operator.
};
#endif // RESET_HANDLER_HPP