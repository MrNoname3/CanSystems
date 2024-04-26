#include "main.hpp"

//--- Variables ---//
const char separator[] PROGMEM = "******************************************************";

#if defined(ESP32) && defined(PROJECT_CAN)
TaskHandle_t canTaskHandle = nullptr;
#endif

//--- Networking ---//
Connectivity iotConn(Serial, SPI_CS, LED, false);

//--- MQTT handler objects ---//
#ifdef PROJECT_RAD_RF
Radiation radiation(iotConn, "radiation", RAD);
RfHandler rfHandler(iotConn, "rf433", RF_RX, RF_TX);
#elif defined PROJECT_CAN
CanHandler canHandler(Serial);
CanAlertDriver canAlert1(canHandler, 26U, iotConn, "alert1", -0.5F);
CanAlertDriver canAlert2(canHandler, 27U, iotConn, "alert2", -0.8F);
#endif

void setup() {
  Serial.printf_P(PSTR("%s\r\nStarting...\r\n"), separator);
  iotConn.begin(Connectivity::Interface::ETHERNET, true);
  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), separator);
#if defined(ESP32) && defined(PROJECT_CAN)
  if(xTaskCreateUniversal(canTask, "canTask", 8192U, nullptr, 1, &canTaskHandle, 0) != pdTRUE) {
    Serial.printf_P(PSTR("Error creating the CAN task!"));
  }
#endif
}

void loop() {
  iotConn.loop();
#ifdef ESP32
  vTaskDelay(5);
#endif
}

#if defined(ESP32) && defined(PROJECT_CAN)
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
#endif