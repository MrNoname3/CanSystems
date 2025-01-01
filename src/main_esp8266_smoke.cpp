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
#include "adcReader.hpp"
#include "mq135Handler.hpp"

//--- Constants ---//
static constexpr uint8_t LED_PIN                    = D8;           // Pin of the LED.
static constexpr uint8_t SPI_CS                     = D0;           // Ethernet shield SPI CS.
static constexpr uint8_t ADC_RDY                    = D2;           // ADC ready signal.
static constexpr uint8_t I2C_SDA                    = D4;
static constexpr uint8_t I2C_SCL                    = D3;

//--- Functions ---//
void maxLoopTimeCallback(uint32_t maxLoopTime);

//--- Driver objects ---//
DebugLedHandler debugLed(LED_PIN, HIGH);
Performance performance(1U, maxLoopTimeCallback);
NetworkManager networkManager(Serial, NetworkManager::Interface::WIFI);
Connectivity iotConn(
  Serial,
  networkManager,
  [](bool state) -> void {
    state ? debugLed.stopTicker() : debugLed.startTicker(250U);
  },
  WdtHandler::resetWatchdog
);

//--- MQTT handler objects ---//
MqttCommon mqttCommon (iotConn, "common", Serial);
AdcReader adcReader(iotConn, "adcreader", 100U, ADC_RDY, I2C_SDA, I2C_SCL);
Mq135Handler mq135(iotConn, "mq135", adcReader, AdcReader::Channel::AN0, 10000U);

//--- Handling tasks ---//
Task *task[5] = {&iotConn, &performance, &mqttCommon, &adcReader, &mq135};
static constexpr uint8_t taskNum = sizeof(task) / sizeof(*task);
TaskHandler<taskNum, false> taskHandler(task);

void setup() {
  const uint32_t initTime = millis();
  WdtHandler::enableWatchdog();
  Serial.begin(MONITOR_BAUD);
  debugLed.startTicker(500U);
  delay(1U);
  Serial.printf_P(PSTR("\r\n%s\r\nStarting...\r\n"), Str::getSectionSeparator());
  Build::printBuildInfo();

  //adcReader.enableMqttSending(10000U);

  const uint32_t initResult = taskHandler.initTasks();
  const bool initSuccess = (initResult == 0U);
  Serial.printf_P(PSTR("Init: %s\r\n"), Str::getStateStr(initSuccess));
  if(!initSuccess) {
    Serial.printf_P(PSTR("  Code: "));
    Serial.println(initResult, BIN);
    ResetHandler::restartMCU();
  }

  Serial.printf_P(PSTR("Init time: %lums\r\n"), (millis() - initTime));
  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), Str::getSectionSeparator());
  debugLed.stopTicker();
  performance.resetTimer();
}

void loop() {
  WdtHandler::resetWatchdog();
  (void)taskHandler.runTasks();
}

void maxLoopTimeCallback(uint32_t maxLoopTime) {
  Serial.printf_P(PSTR("Max loop time: %ums\r\n"), maxLoopTime);
}