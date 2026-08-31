#pragma once
#if defined(ESP8266) || defined(ESP32)

#include <Arduino.h>                                                /// Arduino libraries header.
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "common.hpp"                                               /// Common definitions and functions.
#include "wdtHandler.hpp"                                           /// Handles the watchdog timer.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include "debugLedHandler.hpp"                                      /// Handles the debug LED.
#include "performance.hpp"                                          /// Performance measurement class.
#include "taskHandler.hpp"                                          /// Class for task scheduling.

/// @brief The startup and loop body every ESP node shares.
/// @details A node's main supplies the objects it builds and its task list; everything around
/// that lives here - arming the watchdog, the banner, running initTasks() and restarting when it
/// reports a failure, and the loop's feed-and-run. The platform differences are handled here too:
/// only the ESP32 can report whether a watchdog call took, and only it yields to FreeRTOS at the
/// end of a pass.
///
/// The AVR nodes deliberately keep their own setup(): they print through println(F(...)) rather
/// than printf_P, which on a 32 KB part is a flash decision, and their startup drives node
/// hardware (LED strip, button, external sensor) before the task list exists.
namespace AppBootstrap {

  /// @brief Arms the watchdog.
  /// @return `false` only where the platform can tell, which is the ESP32.
  [[nodiscard]] inline bool armWatchdog() {
#if defined(ESP32)
    return WdtHandler::enableWatchdog();
#else
    WdtHandler::enableWatchdog();
    return true;
#endif
  }

  /// @brief Feeds the watchdog.
  /// @return `false` only where the platform can tell, which is the ESP32.
  [[nodiscard]] inline bool feedWatchdog() {
#if defined(ESP32)
    return WdtHandler::resetWatchdog();
#else
    WdtHandler::resetWatchdog();
    return true;
#endif
  }

  /// @brief Brings the node up: banner, task initialisation and the startup timing report.
  /// @param taskHandler Task handler holding the node's task list.
  /// @param debugLed Debug LED, blinked for the duration of the startup.
  /// @param performance Round-time tracker, whose baseline is taken once the startup is done.
  /// @note Does not return when a watchdog or task initialisation fails: it restarts the MCU.
  template<uint8_t taskNumber, bool fullRoundRobin>
  [[gnu::always_inline]] inline void runSetup(TaskHandler<taskNumber, fullRoundRobin>& taskHandler, DebugLedHandler& debugLed, Performance& performance) {
    const uint32_t initTime = millis();
    const bool wdtEnabled = armWatchdog();
    Serial.begin(MONITOR_BAUD);
    debugLed.startTicker(500U);
    delay(1U);
    Logger::get()->printf_P(PSTR("\r\n%s\r\nStarting...\r\n"), Str::getSectionSeparator());
    Build::printBuildInfo();
    if(!wdtEnabled) {
      Logger::get()->printf_P(PSTR("WDT enable failed!\r\n"));
      ResetHandler::restartMCU();
    }

    const uint32_t initResult = taskHandler.initTasks();
    const bool initSuccess = (initResult == 0U);
    Logger::get()->printf_P(PSTR("Init: %s\r\n"), Str::getStateStr(initSuccess));
    if(!initSuccess) {
      Logger::get()->printf_P(PSTR("  Code: "));
      Logger::get()->println(initResult, BIN);
      ResetHandler::restartMCU();
    }

    Logger::get()->printf_P(PSTR("Init time: %lums\r\n"), (millis() - initTime));
    Logger::get()->printf_P(PSTR("%s\r\nLoop starting...\r\n"), Str::getSectionSeparator());
    debugLed.stopTicker();
    performance.resetTimer();
  }

  /// @brief One pass of the main loop: feed the watchdog, then run the tasks.
  /// @param taskHandler Task handler holding the node's task list.
  template<uint8_t taskNumber, bool fullRoundRobin>
  [[gnu::always_inline]] inline void runLoop(TaskHandler<taskNumber, fullRoundRobin>& taskHandler) {
    if(!feedWatchdog()) {
      Logger::get()->printf_P(PSTR("WDT reset failed!\r\n"));
    }
    (void)taskHandler.runTasks();
#if defined(ESP32)
    taskYIELD();
#endif
  }
} // namespace AppBootstrap

#endif // ESP8266 || ESP32
