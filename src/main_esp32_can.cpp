//--- Headers ---//
#include <Arduino.h>                                                /// Arduino libraries header.
#include "wdtHandler.hpp"                                           /// Handles the watchdog timer.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include "debugLedHandler.hpp"                                      /// Handles the debug LED.
#include "taskHandler.hpp"                                          /// Class for task scheduling.
#include "common.hpp"                                               /// Common definitions and functions.
#include "performance.hpp"                                          /// Performance measurement class.
#include "connectivity.hpp"
#include "radiation.hpp"
#include "rfHandler.hpp"
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
Connectivity iotConn(Serial, debugLed, []() -> void {WdtHandler::resetWatchdog();});

//--- MQTT handler objects ---//
CanHandler canHandler(Serial);
CanAlertDriver canAlert1(canHandler, 26U, iotConn, "alert1", -0.5F);
CanAlertDriver canAlert2(canHandler, 27U, iotConn, "alert2", -0.8F);

//--- Handling tasks ---//
Task *task[1] = {&performance};
static constexpr uint8_t taskNum = sizeof(task) / sizeof(*task);
TaskHandler<taskNum, false> taskHandler(task);

void setup() {
  WdtHandler::enableWatchdog();
  Serial.begin(MONITOR_BAUD);
  delay(1U);
  Serial.printf_P(PSTR("\r\n%s\r\nStarting...\r\n"), Str::getSectionSeparator());
  iotConn.begin(Connectivity::Interface::ETHERNET, true);

  const uint32_t initResult = taskHandler.initTasks();
  const bool initSuccess = (initResult == 0U);
  Serial.printf_P(PSTR("Init:%s\r\n"), Str::getStateStr(initSuccess));
  if(!initSuccess) {
    Serial.printf_P(PSTR("Code: "));
    Serial.println(initResult, BIN);
    ResetHandler::restartMCU();
  }

  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), Str::getSectionSeparator());

  if(xTaskCreateUniversal(canTask, "canTask", 8192U, nullptr, 1, &canTaskHandle, 0) != pdTRUE) {
    Serial.printf_P(PSTR("Error creating the CAN task!"));
  }
}

void loop() {
  WdtHandler::resetWatchdog();
  taskHandler.runTasks();
  iotConn.loop();
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