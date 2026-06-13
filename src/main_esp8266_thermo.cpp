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
#include "mqttCommon.hpp"       /// Basic server <-> client interaction (commands, inbound file transfer).
#include "mqttThermometer.hpp"  /// DS18B20 multi-sensor reader + MQTT publisher.

//--- Constants ---//
// clang-format off
static constexpr uint8_t  LED_PIN           = D4;                 // On-board status LED (active-low; adjust per board).
static constexpr uint8_t  ONE_WIRE_PIN      = D1;                 // DS18B20 1-Wire data line (4.7k pull-up to 3V3).
static constexpr uint8_t  MAX_THERMOMETERS  = 8U;                 // Compile-time upper bound on DS18B20 sensors.
static constexpr uint32_t MEASURE_PERIOD_MS = Time::minToMs(5U);  // Interval between measurement cycles.
// clang-format on

//--- Functions ---//
void maxLoopTimeCallback(uint32_t maxLoopTime);

//--- Driver objects ---//
DebugLedHandler debugLed(LED_PIN, LOW);
Performance performance(1U, maxLoopTimeCallback);
NetworkManager networkManager(NetworkManager::Interface::WIFI);
Connectivity iotConn(
    networkManager,
    [](bool state) -> void {
      state ? debugLed.stopTicker() : debugLed.startTicker(250U);
    },
    WdtHandler::resetWatchdog);

//--- MQTT handler objects ---//
MqttCommon mqttCommon(iotConn, "common");
MqttThermometer<MAX_THERMOMETERS> thermometer(iotConn, "temp", ONE_WIRE_PIN, MEASURE_PERIOD_MS);

//--- Handling tasks ---//
Task* task[] = { &iotConn, &performance, &mqttCommon, &thermometer };
static constexpr uint8_t taskNum = arraySize(task);
TaskHandler<taskNum, false> taskHandler(task);

void setup() {
  const uint32_t initTime = millis();
  WdtHandler::enableWatchdog();
  Serial.begin(MONITOR_BAUD);
  debugLed.startTicker(500U);
  delay(1U);
  Logger::get()->printf_P(PSTR("\r\n%s\r\nStarting...\r\n"), Str::getSectionSeparator());
  Build::printBuildInfo();

  const uint32_t initResult = taskHandler.initTasks();
  const bool initSuccess = (initResult == 0U);
  Logger::get()->printf_P(PSTR("Init:%s\r\n"), Str::getStateStr(initSuccess));
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

void loop() {
  WdtHandler::resetWatchdog();
  (void)taskHandler.runTasks();
}

void maxLoopTimeCallback(uint32_t maxLoopTime) {
  Logger::get()->printf_P(PSTR("Max loop time: %ums\r\n"), maxLoopTime);
}
