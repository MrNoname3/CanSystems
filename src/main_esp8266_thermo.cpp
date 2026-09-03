//--- Headers ---//
#include <Arduino.h>            /// Arduino libraries header.
#include "wdtHandler.hpp"       /// Handles the watchdog timer.
#include "debugLedHandler.hpp"  /// Handles the debug LED.
#include "taskHandler.hpp"      /// Class for task scheduling.
#include "appBootstrap.hpp"     /// Shared ESP startup and loop body.
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
void maxRoundTimeCallback(uint32_t maxRoundTime);

//--- Driver objects ---//
DebugLedHandler debugLed(LED_PIN, LOW);
Performance performance(1U, maxRoundTimeCallback);
NetworkManager networkManager(NetworkManager::Interface::WIFI);
Connectivity iotConn(
    networkManager,
    [](bool state) -> void {
      state ? debugLed.stopTicker() : debugLed.startTicker(250U);
    },
    WdtHandler::resetWatchdog);

//--- MQTT handler objects ---//
MqttCommon mqttCommon(iotConn, MqttTopics::getCommonSubtopic());
MqttThermometer<MAX_THERMOMETERS> thermometer(iotConn, "temp", ONE_WIRE_PIN, MEASURE_PERIOD_MS);

//--- Handling tasks ---//
Task* task[] = { &iotConn, &performance, &mqttCommon, &thermometer };
static constexpr uint8_t taskNum = arraySize(task);
TaskHandler<taskNum, false> taskHandler(task);

void setup() {
  AppBootstrap::runSetup(taskHandler, debugLed, performance);
}

void loop() {
  AppBootstrap::runLoop(taskHandler);
}

void maxRoundTimeCallback(uint32_t maxRoundTime) {
  Logger::get()->printf_P(PSTR("Max round time: %ums\r\n"), maxRoundTime);
}
