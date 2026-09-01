#include "resetHandler.hpp"
#if defined(__AVR_ATmega328P__)
#include <avr/wdt.h>                                                /// Watchdog timer library for AVR microcontrollers.
#include <avr/io.h>                                                 /// MCUSR for the reset flags.
#elif defined(ESP8266)
#include <Esp.h>                                                    /// Restart and reset-related functions for ESP8266.
#include <user_interface.h>                                         /// Provides the definition for struct rst_info.
#elif defined(ESP32)
#include <Esp.h>                                                    /// Restart and reset-related functions for ESP32.
#include <esp_task_wdt.h>                                           /// Watchdog timer functions specific to ESP32.
#include <esp_system.h>                                             /// Provides esp_reset_reason() and esp_reset_reason_t.
#endif
#include "common.hpp"                                               /// Common definitions and functions.

#if defined(__AVR_ATmega328P__)
namespace {
  // .noinit survives a reset; volatile keeps the .init3 stores from being dropped as dead, which
  // would also free the register holding the r2 read.
  volatile uint8_t bootResetFlags __attribute__((section(".noinit")));
  volatile uint16_t restartMarker __attribute__((section(".noinit")));
  volatile uint8_t restartCause __attribute__((section(".noinit")));
  constexpr uint16_t restartMagic = 0xB007U;   // Tells the marker apart from uninitialised SRAM.
} // namespace

// .init3 runs after the stack is up but before .data/.bss and the global constructors, so this
// reads r2 before the compiler can use it and reaches the watchdog before WdtHandler's constructor.
void captureResetFlags() __attribute__((naked, used, section(".init3")));
void captureResetFlags() {
  uint8_t handedOver;
  __asm__ __volatile__("mov %0, r2" : "=r"(handedOver));   // where urboot leaves MCUSR before clearing it
  const uint8_t ownFlags = MCUSR;                         // still set when no bootloader handed it over
  MCUSR = 0;                                          // Must be cleared before WDE can be.
  wdt_disable();
  // A real reset sets one of the low four bits and none of the top four; anything else is
  // register leftovers, which is what a bootloader that clears MCUSR without passing it on leaves.
  const bool handoverLooksReal = (handedOver != 0U) && ((handedOver & 0xF0U) == 0U);
  uint8_t flags = ownFlags;
  if(ownFlags == 0U) { flags = handoverLooksReal ? handedOver : 0U; }
  if(restartMarker == restartMagic) {
    flags |= ResetHandler::intentionalRestartFlag;
    flags |= static_cast<uint8_t>((restartCause & 0x07U) << ResetHandler::restartCauseShift);
  }
  restartMarker = 0U;                                 // One restart, one flag.
  bootResetFlags = flags;
}

uint8_t ResetHandler::getResetReason() {  // NOLINT(readability-convert-member-functions-to-static) declared static in the header
  return bootResetFlags;
}
#endif

#if defined(__AVR_ATmega328P__)
void ResetHandler::restartMCU() {
  restartMCU(RestartCause::Unspecified);
}

void ResetHandler::restartMCU(RestartCause cause) {
  Logger::get()->println(F("Restarting..."));
  Logger::get()->flush();                              // Sends out data from the serial buffer before reset.
  restartCause = static_cast<uint8_t>(cause) & 0x07U;  // Three bits, so the shift cannot reach MCUSR's.
  restartMarker = restartMagic;                        // Tells the next startup this reset was asked for.
  wdt_enable(WDTO_15MS);                               // Configures the watchdog timer for a 15-ms timeout.
  while(true) {}
}
#elif defined(ESP32) || defined(ESP8266)
void ResetHandler::restartMCU() {
  Logger::get()->printf_P(PSTR("Restarting...\r\n"));
  Logger::get()->flush();
  ESP.restart();
  while(true) {}
}
#endif

#if defined(ESP8266) || defined(ESP32)
uint8_t ResetHandler::getResetReason() {
#ifdef ESP8266
  return static_cast<uint8_t>(ESP.getResetInfoPtr()->reason);
#elif defined ESP32
  return static_cast<uint8_t>(esp_reset_reason());
#endif
}

bool ResetHandler::isWdtReset() {
#ifdef ESP8266
  const uint8_t reason = getResetReason();
  return (reason == REASON_WDT_RST) || (reason == REASON_SOFT_WDT_RST); // hardware WDT or software WDT
#elif defined(ESP32)
  const uint8_t reason = getResetReason();
  return (reason == ESP_RST_INT_WDT) || (reason == ESP_RST_TASK_WDT) || (reason == ESP_RST_WDT); // interrupt WDT, task WDT, or RTC WDT
#endif
}
#endif
