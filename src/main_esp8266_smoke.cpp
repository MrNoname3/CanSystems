//--- Headers ---//
#include <Arduino.h>                                                /// Arduino libraries header.
#include "wdtHandler.hpp"                                           /// Handles the watchdog timer.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include "debugLedHandler.hpp"                                      /// Handles the debug LED.
#include "taskHandler.hpp"                                          /// Class for task scheduling.
#include "common.hpp"                                               /// Common definitions and functions.
#include "performance.hpp"                                          /// Performance measurement class.
#include "connectivity.hpp"
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

//--- Variables ---//
const char separator[] PROGMEM = "******************************************************";

//--- Driver objects ---//
DebugLedHandler debugLed(LED_PIN, HIGH);
Performance performance(1U, maxLoopTimeCallback);
Connectivity iotConn(Serial, debugLed, WdtHandler::resetWatchdog, SPI_CS);

//--- MQTT handler objects ---//
AdcReader adcReader(iotConn, "adcreader", 100U, ADC_RDY, I2C_SDA, I2C_SCL);
Mq135Handler mq135(iotConn, "mq135", adcReader, AdcReader::Channel::AN0, 10000U);

//--- Handling tasks ---//
Task *task[1] = {&performance};
static constexpr uint8_t taskNum = sizeof(task) / sizeof(*task);
TaskHandler<taskNum, false> taskHandler(task);

void setup() {
  WdtHandler::enableWatchdog();
  Serial.begin(MONITOR_BAUD);
  delay(1U);
  Serial.printf_P(PSTR("%s\r\nStarting...\r\n"), separator);
  iotConn.begin(Connectivity::Interface::WIFI, true);
  //adcReader.enableMqttSending(10000U);

  const uint32_t initResult = taskHandler.initTasks();
  const bool initSuccess = (initResult == 0U);
  Serial.printf_P(PSTR("Init: %s\r\n"), Str::getStateStr(initSuccess));
  if(!initSuccess) {
    Serial.printf_P(PSTR("Code: "));
    Serial.println(initResult, BIN);
    ResetHandler::restartMCU();
  }

  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), separator);
}

void loop() {
  WdtHandler::resetWatchdog();
  taskHandler.runTasks();
  iotConn.loop();
}

void maxLoopTimeCallback(uint32_t maxLoopTime) {
  Serial.printf_P(PSTR("Max loop time: %ums\r\n"), maxLoopTime);
}