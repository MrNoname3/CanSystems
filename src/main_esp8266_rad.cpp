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
#include "radiation.hpp"      /// Reads the radiation meter.
#include "rfHandler.hpp"      /// Handles the 433 MHz transceiver.

//--- Constants ---//
// clang-format off
static constexpr uint8_t LED_PIN                    = D8;           // Pin of the LED.
static constexpr uint8_t SPI_CS                     = D0;           // Ethernet shield SPI CS.
static constexpr uint8_t RAD                        = D2;           // Radiation meter.
static constexpr uint8_t RF_RX                      = D1;           // RF receive pin.
static constexpr uint8_t RF_TX                      = D3;           // RF transmit pin.
// clang-format on

//--- Functions ---//
void maxRoundTimeCallback(uint32_t maxRoundTime);

//--- Driver objects ---//
DebugLedHandler debugLed(LED_PIN, HIGH);
Performance performance(1U, maxRoundTimeCallback);
NetworkManager networkManager(NetworkManager::Interface::ENC28J60, SPI_CS);
Connectivity iotConn(
    networkManager,
    [](bool state) -> void {
      state ? debugLed.stopTicker() : debugLed.startTicker(250U);
    },
    WdtHandler::resetWatchdog);

//--- MQTT handler objects ---//
MqttCommon mqttCommon(iotConn, "common");
Radiation radiation(iotConn, "radiation", RAD);
RfHandler rfHandler(iotConn, "rf433", RF_RX, RF_TX);

//--- Handling tasks ---//
Task* task[] = { &iotConn, &performance, &mqttCommon, &radiation, &rfHandler };
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
