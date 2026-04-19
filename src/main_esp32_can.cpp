//--- Headers ---//
#include <Arduino.h>            /// Arduino libraries header.
#include "wdtHandler.hpp"       /// Handles the watchdog timer.
#include "resetHandler.hpp"     /// Handles MCU reset from the program.
#include "debugLedHandler.hpp"  /// Handles the debug LED.
#include "taskHandler.hpp"      /// Class for task scheduling.
#include "common.hpp"           /// Common definitions and functions.
#include "performance.hpp"      /// Performance measurement class.
#include "networkManager.hpp"   /// Manages the network connection.
#include "connectivity.hpp"     /// Handles the MQTT connection.
#include "mqttCommon.hpp"       /// Handles the basic interaction between server and client.
#include "canHandler.hpp"       /// CAN handler library.
#include "canAlertDriver.hpp"   /// Driver for the alert client.

//--- Constants ---//
// clang-format off
static constexpr uint8_t LED_PIN                    = 2U;           // Pin of the LED.
// clang-format on

//--- Functions ---//
void maxLoopTimeCallback(uint32_t maxLoopTime);

//--- Driver objects ---//
DebugLedHandler debugLed(LED_PIN, HIGH);
Performance performance(2U, maxLoopTimeCallback);
NetworkManager networkManager(NetworkManager::Interface::LAN8720);
Connectivity iotConn(
    networkManager,
    [](bool state) -> void {
      state ? debugLed.stopTicker() : debugLed.startTicker(250U);
    },
    []() -> void {
      (void)WdtHandler::resetWatchdog();
    });

//--- MQTT handler objects ---//
MqttCommon mqttCommon(iotConn, "common");
CanHandler canHandler;
CanAlertDriver canAlert1(canHandler, 26U, iotConn, "alert1", -0.5F);
CanAlertDriver canAlert2(canHandler, 27U, iotConn, "alert2", -0.8F);

//--- Handling tasks ---//
Task* task[] = { &iotConn, &performance, &mqttCommon, &canHandler, &canAlert1, &canAlert2 };
static constexpr uint8_t taskNum = arraySize(task);
TaskHandler<taskNum, false> taskHandler(task);

void setup() {
  const uint32_t initTime = millis();
  const bool wdtEnabled = WdtHandler::enableWatchdog();
  Serial.begin(MONITOR_BAUD);
  debugLed.startTicker(500U);
  delay(1U);
  Logger::get().printf_P(PSTR("\r\n%s\r\nStarting...\r\n"), Str::getSectionSeparator());
  Build::printBuildInfo();
  if (!wdtEnabled) {
    Logger::get().printf_P(PSTR("WDT enable failed!\r\n"));
    ResetHandler::restartMCU();
  }

  const uint32_t initResult = taskHandler.initTasks();
  const bool initSuccess = (initResult == 0U);
  Logger::get().printf_P(PSTR("Init:%s\r\n"), Str::getStateStr(initSuccess));
  if (!initSuccess) {
    Logger::get().printf_P(PSTR("  Code: "));
    Logger::get().println(initResult, BIN);
    ResetHandler::restartMCU();
  }

  Logger::get().printf_P(PSTR("Init time: %lums\r\n"), (millis() - initTime));
  Logger::get().printf_P(PSTR("%s\r\nLoop starting...\r\n"), Str::getSectionSeparator());
  debugLed.stopTicker();
  performance.resetTimer();
}

void loop() {
  if (!WdtHandler::resetWatchdog()) {
    Logger::get().printf_P(PSTR("WDT reset failed!\r\n"));
  }
  (void)taskHandler.runTasks();
  taskYIELD();
}

void maxLoopTimeCallback(uint32_t maxLoopTime) {
  Logger::get().printf_P(PSTR("Max loop time: %ums\r\n"), maxLoopTime);
}