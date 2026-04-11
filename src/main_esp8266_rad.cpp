//--- Headers ---//
#include <Arduino.h>                                                /// Arduino libraries header.
#include "wdtHandler.hpp"                                           /// Handles the watchdog timer.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include "debugLedHandler.hpp"                                      /// Handles the debug LED.
#include "taskHandler.hpp"                                          /// Class for task scheduling.
#include "common.hpp"                                               /// Common definitions and functions.
#include "performance.hpp"                                          /// Performance measurement class.
#include "networkManager.hpp"                                       /// Manages the network connection.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "mqttCommon.hpp"                                           /// Handles the basic interaction between server and client.
#include "radiation.hpp"
#include "rfHandler.hpp"

//--- Constants ---//
static constexpr uint8_t LED_PIN                    = D8;           // Pin of the LED.
static constexpr uint8_t SPI_CS                     = D0;           // Ethernet shield SPI CS.
static constexpr uint8_t RAD                        = D2;           // Radiation meter.
static constexpr uint8_t RF_RX                      = D1;           // RF receive pin.
static constexpr uint8_t RF_TX                      = D3;           // RF transmit pin.

//--- Functions ---//
void maxLoopTimeCallback(uint32_t maxLoopTime);

//--- Driver objects ---//
DebugLedHandler debugLed(LED_PIN, HIGH);
Performance performance(1U, maxLoopTimeCallback);
NetworkManager networkManager(NetworkManager::Interface::ENC28J60, SPI_CS);
Connectivity iotConn(
  networkManager,
  [](bool state) -> void {
    state ? debugLed.stopTicker() : debugLed.startTicker(250U);
  },
  WdtHandler::resetWatchdog
);

//--- MQTT handler objects ---//
MqttCommon<> mqttCommon (iotConn, "common");
Radiation radiation(iotConn, "radiation", RAD);
RfHandler rfHandler(iotConn, "rf433", RF_RX, RF_TX);

//--- Handling tasks ---//
Task *task[] = {&iotConn, &performance, &mqttCommon, &radiation, &rfHandler};
static constexpr uint8_t taskNum = arraySize(task);
TaskHandler<taskNum, false> taskHandler(task);

void setup() {
  const uint32_t initTime = millis();
  WdtHandler::enableWatchdog();
  Serial.begin(MONITOR_BAUD);
  debugLed.startTicker(500U);
  delay(1U);
  Logger::get().printf_P(PSTR("\r\n%s\r\nStarting...\r\n"), Str::getSectionSeparator());
  Build::printBuildInfo();

  const uint32_t initResult = taskHandler.initTasks();
  const bool initSuccess = (initResult == 0U);
  Logger::get().printf_P(PSTR("Init:%s\r\n"), Str::getStateStr(initSuccess));
  if(!initSuccess) {
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
  WdtHandler::resetWatchdog();
  (void)taskHandler.runTasks();
}

void maxLoopTimeCallback(uint32_t maxLoopTime) {
  Logger::get().printf_P(PSTR("Max loop time: %ums\r\n"), maxLoopTime);
}