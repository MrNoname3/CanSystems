#include "main.hpp"

//--- Variables ---//
const char separator[] PROGMEM = "******************************************************";

#ifdef ESP32
TaskHandle_t mainTaskHandle = nullptr;
#endif

//--- Networking ---//
Connectivity iotConn(Serial, SPI_CS, LED, false);

//--- MQTT handler objects ---//
#ifdef PROJECT_RAD_RF
Radiation radiation(iotConn, "radiation", RAD);
RfHandler rfHandler(iotConn, "rf433", RF_RX, RF_TX);
#elif defined PROJECT_CAN
CanHandler canHandler(Serial, false);
#endif

void setup() {
  Serial.printf_P(PSTR("%s\r\nStarting...\r\n"), separator);
#ifdef ESP8266
  iotConn.begin(Connectivity::Interface::ETHERNET, true);
  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), separator);
#elif defined ESP32
  if(xTaskCreateUniversal(mainTask, "mainTask", 8192U, nullptr, 1, &mainTaskHandle, 0) != pdTRUE) {
    Serial.printf_P(PSTR("Error creating the main task!"));
  }
  vTaskDelete(nullptr);
#endif
}

void loop() {
#ifdef ESP8266
  iotConn.loop();
#endif
}

#ifdef ESP32
void mainTask(void *pvParameters) {
  iotConn.begin(Connectivity::Interface::ETHERNET, true);
  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), separator);
  size_t stackSize = uxTaskGetStackHighWaterMark(NULL);
  Serial.print("Configured stack size: ");
  Serial.println(stackSize);
  while(true) {
    iotConn.loop();
    vTaskDelay(5);
  }
  vTaskDelete(nullptr);
}
#endif