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
#include "mqttUploader.hpp"     /// Outbound (client -> server) file upload handler.
#include "cameraHandler.hpp"    /// Periodic camera capture -> upload queue.
#include "mqttThermometer.hpp"  /// DS18B20 multi-sensor temperature reporting.

//--- Constants ---//
// clang-format off
static constexpr uint8_t  LED_PIN              = 33U;               // On-board (red) status LED on the ESP32-CAM.
static constexpr uint32_t CAPTURE_INTERVAL_MS  = Time::minToMs(1U); // Time between captures (skeleton default: 1 minute).
static constexpr uint8_t  DS18B20_PIN          = 13U;              // 1-Wire data pin (free GPIO on the ESP32-CAM; needs a 4.7k pull-up).
static constexpr uint8_t  MAX_THERMOMETERS     = 4U;               // Compile-time cap on DS18B20 sensors.
static constexpr uint32_t TEMP_INTERVAL_MS     = Time::secToMs(30U); // Time between temperature measurements.
// clang-format on

//--- Functions ---//
void maxLoopTimeCallback(uint32_t maxLoopTime);

//--- Driver objects ---//
DebugLedHandler debugLed(LED_PIN, HIGH);
Performance performance(2U, maxLoopTimeCallback);
NetworkManager networkManager(NetworkManager::Interface::WIFI);
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
MqttUploader mqttUploader(iotConn, "upload");

//--- Application objects ---//
CameraHandler camera(mqttUploader, CAPTURE_INTERVAL_MS);
MqttThermometer<MAX_THERMOMETERS> thermometer(iotConn, "temp", DS18B20_PIN, TEMP_INTERVAL_MS);

//--- Handling tasks ---//
Task* task[] = { &iotConn, &performance, &mqttCommon, &mqttUploader, &camera, &thermometer };
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
