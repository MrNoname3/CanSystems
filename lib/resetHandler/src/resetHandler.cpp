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
  // Survives a reset because .noinit is left alone by .init4, and SRAM keeps its contents through
  // everything but a power cycle.
  // volatile is load-bearing, not decoration: these are written in .init3 and read from an
  // unrelated context much later, and without it the optimiser drops the stores as dead - which
  // also frees it to reuse the register the r2 read landed in.
  volatile uint8_t bootResetFlags __attribute__((section(".noinit")));
  volatile uint16_t restartMarker __attribute__((section(".noinit")));
  constexpr uint16_t restartMagic = 0xB007U;   // Distinguishes our marker from uninitialised SRAM.
} // namespace

// Runs from .init3: the stack is up (.init2) but .data/.bss (.init4) and the global constructors
// (.init6) have not run, so this beats the WdtHandler constructor to the watchdog and reads r2
// before the compiler can use it for anything else.
//
// urboot copies MCUSR into r2 and then clears MCUSR (verified in the shipped bootloader images),
// which is why reading MCUSR from the application only ever returns 0. It also disables the
// watchdog itself, so the wdt_disable() below only matters for a board programmed over ISP with
// no bootloader - and on such a board r2 is undefined at power-up, which is what the MCUSR
// fallback covers: urboot always leaves MCUSR at 0, so a non-zero one means nobody handed over.
void captureResetFlags() __attribute__((naked, used, section(".init3")));
void captureResetFlags() {
  uint8_t handedOver;
  __asm__ __volatile__("mov %0, r2" : "=r"(handedOver));
  const uint8_t ownFlags = MCUSR;
  MCUSR = 0;                                          // Must be cleared before WDE can be.
  wdt_disable();
  uint8_t flags = (ownFlags != 0U) ? ownFlags : handedOver;
  if(restartMarker == restartMagic) { flags |= ResetHandler::intentionalRestartFlag; }
  restartMarker = 0U;                                 // One restart, one flag.
  bootResetFlags = flags;
}

uint8_t ResetHandler::getResetReason() {  // NOLINT(readability-convert-member-functions-to-static) declared static in the header
  return bootResetFlags;
}
#endif

void ResetHandler::restartMCU() {
#if defined(__AVR_ATmega328P__)
  Logger::get()->println(F("Restarting..."));
  Logger::get()->flush();                              // Sends out data from the serial buffer before reset.
  restartMarker = restartMagic;                       // Tells the next startup this reset was asked for.
  wdt_enable(WDTO_15MS);                              // Configures the watchdog timer for a 15-ms timeout.
#elif defined(ESP32) || defined(ESP8266)
  Logger::get()->printf_P(PSTR("Restarting...\r\n"));
  Logger::get()->flush();
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
