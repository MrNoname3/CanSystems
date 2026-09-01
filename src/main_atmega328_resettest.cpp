//--- Headers ---//
// Bench firmware for the part of the reset capture a build cannot check: that SRAM carries
// .noinit across a reset, and that the marker separates restartMCU() from a lock-up.
//
//   pio run -e nanoatmega328_resettest -t upload --upload-port /dev/ttyUSB0
//   pio device monitor -p /dev/ttyUSB0 -b 115200
//
// 'r' restarts through restartMCU(), 'h' hangs until the watchdog fires, 'z' clears the counter.
// Both are watchdog resets; only the restart may carry intentionalRestartFlag.
#include <Arduino.h>          /// Arduino libraries header.
#include "wdtHandler.hpp"     /// Handles the watchdog timer.
#include "resetHandler.hpp"   /// Reset reason captured during startup.
#include "common.hpp"         /// Common definitions and functions.

//--- Bench state ---//
// .noinit for the same reason resetHandler uses it: .init4 would clear anything in .bss, and the
// point of the counter is to show what survives a reset that is not a power cycle.
namespace {
  uint16_t bootCounter __attribute__((section(".noinit")));
  uint16_t bootCounterMagic __attribute__((section(".noinit")));
  // Opening the serial port asserts DTR and resets the board, so a power-on banner can never be
  // read live. Carrying the previous reason forward is the only way to see one.
  uint8_t previousReason __attribute__((section(".noinit")));
  constexpr uint16_t counterMagic = 0xC0DEU;
} // namespace

//--- Driver objects ---//
WdtHandler wdt(WdtHandler::WDT::T_1S);

void printReason(uint8_t reason) {
  Logger::get()->print(F("Reset reason: 0x"));
  if(reason < 0x10U) { Logger::get()->print('0'); }
  Logger::get()->print(reason, HEX);
  Logger::get()->print(F("  ["));
  if((reason & 0x01U) != 0U) { Logger::get()->print(F(" PORF")); }
  if((reason & 0x02U) != 0U) { Logger::get()->print(F(" EXTRF")); }
  if((reason & 0x04U) != 0U) { Logger::get()->print(F(" BORF")); }
  if((reason & 0x08U) != 0U) { Logger::get()->print(F(" WDRF")); }
  if((reason & ResetHandler::intentionalRestartFlag) != 0U) { Logger::get()->print(F(" INTENTIONAL")); }
  if(reason == 0U) { Logger::get()->print(F(" none - the bootloader kept it")); }
  Logger::get()->print(F(" ] cause: "));
  Logger::get()->println(static_cast<uint8_t>(ResetHandler::getRestartCause(reason)));
}

void setup() {
  WdtHandler::resetWatchdog();  // cppcheck-suppress ignoredReturnValue
  Serial.begin(MONITOR_BAUD);
  delay(1U);

  // A power cycle leaves SRAM undefined, so the counter only means something once the magic is
  // there to say a previous run wrote it.
  if(bootCounterMagic != counterMagic) {
    bootCounterMagic = counterMagic;
    bootCounter = 0U;
    previousReason = 0U;
    Logger::get()->println(F("\r\n******** cold start: boot counter initialised"));
  } else {
    bootCounter++;
  }

  Logger::get()->println(F("\r\n******** reset capture bench"));
  Build::printBuildInfo();
  printReason(ResetHandler::getResetReason());
  Logger::get()->print(F("Boots since the counter was set: "));
  Logger::get()->println(bootCounter);
  if(bootCounter > 0U) {
    Logger::get()->print(F("Previous start: "));
    printReason(previousReason);
  }
  previousReason = ResetHandler::getResetReason();
  Logger::get()->println(F("r = restart   1/2/3 = restart as InitFailed/CommandedOverCan/OtaComplete"));
  Logger::get()->println(F("h = hang until the watchdog fires   z = clear counter"));
}

void loop() {
  WdtHandler::resetWatchdog();  // cppcheck-suppress ignoredReturnValue
  if(Serial.available() == 0) { return; }

  const int command = Serial.read();
  switch(command) {
    case 'r': {
      Logger::get()->println(F("-> restartMCU(): expect the intentional flag on the way back"));
      Logger::get()->flush();
      ResetHandler::restartMCU();
    } break;
    case 'h': {
      Logger::get()->println(F("-> hanging: expect a watchdog reset WITHOUT the intentional flag"));
      Logger::get()->flush();
      // No watchdog reset here, and no marker either - this is what a real lock-up looks like.
      while(true) {}
    }
    case '1': ResetHandler::restartMCU(ResetHandler::RestartCause::InitFailed); break;
    case '2': ResetHandler::restartMCU(ResetHandler::RestartCause::CommandedOverCan); break;
    case '3': ResetHandler::restartMCU(ResetHandler::RestartCause::OtaComplete); break;
    case 'z': {
      bootCounterMagic = 0U;
      Logger::get()->println(F("-> counter cleared; the next start reports a cold one"));
    } break;
    default: {
    } break;
  }
}
