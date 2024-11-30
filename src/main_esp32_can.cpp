//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.
#include "connectivity.hpp"
#include "radiation.hpp"
#include "rfHandler.hpp"
#include "canHandler.hpp"
#include "canAlertDriver.hpp"

//--- Constants ---//
static constexpr const uint8_t LED                    = 2;            // Status LED.
static constexpr const uint8_t SPI_CS                 = -1;           // Ethernet shield SPI CS.

//--- Functions ---//
void canTask(void *pvParameters);

//--- Variables ---//
const char separator[] PROGMEM = "******************************************************";
TaskHandle_t canTaskHandle = nullptr;

//--- Networking ---//
Connectivity iotConn(Serial, SPI_CS, LED, false);

//--- MQTT handler objects ---//
CanHandler canHandler(Serial);
CanAlertDriver canAlert1(canHandler, 26U, iotConn, "alert1", -0.5F);
CanAlertDriver canAlert2(canHandler, 27U, iotConn, "alert2", -0.8F);

void setup() {
  Serial.printf_P(PSTR("%s\r\nStarting...\r\n"), separator);
  iotConn.begin(Connectivity::Interface::ETHERNET, true);
  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), separator);

  if(xTaskCreateUniversal(canTask, "canTask", 8192U, nullptr, 1, &canTaskHandle, 0) != pdTRUE) {
    Serial.printf_P(PSTR("Error creating the CAN task!"));
  }
}

void loop() {
  iotConn.loop();
  vTaskDelay(5);
}

void canTask(void *pvParameters) {
  Serial.printf_P(PSTR("%s\r\nStarting CAN task...\r\n"), separator);
  canHandler.begin(500E3);
  Serial.printf_P(PSTR("%s\r\nCAN loop starting...\r\n"), separator);
  while(true) {
    canHandler.loop();
    vTaskDelay(5);
  }
  vTaskDelete(nullptr);
}