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
  AppBootstrap::runSetup(taskHandler, debugLed, performance);
}

void loop() {
  AppBootstrap::runLoop(taskHandler);
}

void maxLoopTimeCallback(uint32_t maxLoopTime) {
  Logger::get()->printf_P(PSTR("Max loop time: %ums\r\n"), maxLoopTime);
}
