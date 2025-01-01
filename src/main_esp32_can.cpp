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
#include "canHandler.hpp"
#include "canAlertDriver.hpp"

//--- Constants ---//
static constexpr uint8_t LED_PIN                    = 2U;           // Pin of the LED.

//--- Functions ---//
void maxLoopTimeCallback(uint32_t maxLoopTime);
void canTask(void *pvParameters);

//--- Variables ---//
TaskHandle_t canTaskHandle = nullptr;

//--- Driver objects ---//
DebugLedHandler debugLed(LED_PIN, HIGH);
Performance performance(1U, maxLoopTimeCallback);
NetworkManager networkManager(Serial, NetworkManager::Interface::LAN8720);
Connectivity iotConn(
  Serial,
  networkManager,
  [](bool state) -> void {
    state ? debugLed.stopTicker() : debugLed.startTicker(250U);
  },
  []() -> void {
    WdtHandler::resetWatchdog();
  }
);

//--- MQTT handler objects ---//
MqttCommon mqttCommon (iotConn, "common", Serial);
CanHandler canHandler(Serial);
CanAlertDriver canAlert1(canHandler, 26U, iotConn, "alert1", -0.5F);
CanAlertDriver canAlert2(canHandler, 27U, iotConn, "alert2", -0.8F);

//--- Handling tasks ---//
Task *task[3] = {&iotConn, &performance, &mqttCommon};
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

  const uint32_t initResult = taskHandler.initTasks();
  const bool initSuccess = (initResult == 0U);
  Serial.printf_P(PSTR("Init:%s\r\n"), Str::getStateStr(initSuccess));
  if(!initSuccess) {
    Serial.printf_P(PSTR("  Code: "));
    Serial.println(initResult, BIN);
    ResetHandler::restartMCU();
  }

  Serial.printf_P(PSTR("Init time: %lums\r\n"), (millis() - initTime));
  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), Str::getSectionSeparator());
  debugLed.stopTicker();

  if(xTaskCreateUniversal(canTask, "canTask", 8192U, nullptr, 1, &canTaskHandle, 0) != pdTRUE) {
    Serial.printf_P(PSTR("Error creating the CAN task!"));
  }
  performance.resetTimer();
}

void loop() {
  WdtHandler::resetWatchdog();
  (void)taskHandler.runTasks();
  vTaskDelay(5);
}

void canTask(void *pvParameters) {
  Serial.printf_P(PSTR("%s\r\nStarting CAN task...\r\n"), Str::getSectionSeparator());
  canHandler.begin(500E3);
  Serial.printf_P(PSTR("%s\r\nCAN loop starting...\r\n"), Str::getSectionSeparator());
  while(true) {
    canHandler.loop();
    vTaskDelay(5);
  }
  vTaskDelete(nullptr);
}

void maxLoopTimeCallback(uint32_t maxLoopTime) {
  Serial.printf_P(PSTR("Max loop time: %ums\r\n"), maxLoopTime);
}