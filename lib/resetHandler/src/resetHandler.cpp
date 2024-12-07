#include "resetHandler.hpp"
#include <Arduino.h>                                                /// Arduino libraries header.
#if defined(__AVR_ATmega328P__)
#include <avr/wdt.h>                                                /// Watchdog timer library.
#elif defined(ESP8266) || defined(ESP32)
#include <Esp.h>                                                    /// Watchdog timer functions for ESPs.
#endif

void ResetHandler::restartMCU() {
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