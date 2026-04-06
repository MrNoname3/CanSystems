#include "resetHandler.hpp"
#if defined(__AVR_ATmega328P__)
#include <avr/wdt.h>                                                /// Watchdog timer library for AVR microcontrollers.
#elif defined(ESP8266)
#include <Esp.h>                                                    /// Restart and reset-related functions for ESP8266.
#include <user_interface.h>                                         /// Provides the definition for struct rst_info.
#elif defined(ESP32)
#include <Esp.h>                                                    /// Restart and reset-related functions for ESP32.
#include <esp_task_wdt.h>                                           /// Watchdog timer functions specific to ESP32.
#endif
#include "common.hpp"                                               /// Common definitions and functions.

void ResetHandler::restartMCU() {
#if defined(__AVR_ATmega328P__)
  Logger::get().println(F("Restarting..."));
  Logger::get().flush();                              // Sends out data from the serial buffer before reset.
  wdt_enable(WDTO_15MS);                              // Configures the watchdog timer for a 15-ms timeout.
#elif defined(ESP32) || defined(ESP8266)
  Logger::get().printf_P(PSTR("Restarting...\r\n"));
  Logger::get().flush();
  ESP.restart();
#endif
  while(true) {}
}

#if defined(ESP8266) || defined(ESP32)
uint8_t ResetHandler::getResetReason() {
#ifdef ESP8266
  return static_cast<uint8_t>(ESP.getResetInfoPtr()->reason);
#elif defined ESP32
  return static_cast<uint8_t>(esp_reset_reason());
#endif
}
#endif