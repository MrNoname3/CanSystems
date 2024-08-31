//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.
#include "connectivity/src/connectivity.hpp"
#include "adcReader/src/adcReader.hpp"

//--- Constants ---//
static constexpr const uint8_t LED                    = D8;           // Status LED.
static constexpr const uint8_t SPI_CS                 = D0;           // Ethernet shield SPI CS.
static constexpr const uint8_t ADC_RDY                = D3;           // ADC ready signal.

//--- Variables ---//
const char separator[] PROGMEM = "******************************************************";

//--- Networking ---//
Connectivity iotConn(Serial, SPI_CS, LED, false);

//--- MQTT handler objects ---//
AdcReader adcReader(iotConn, "adcreader", 50U, ADC_RDY);

void setup() {
  Serial.printf_P(PSTR("%s\r\nStarting...\r\n"), separator);
  iotConn.begin(Connectivity::Interface::WIFI, true);
  adcReader.enableMqttSending(10000U);
  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), separator);
}

void loop() {
  iotConn.loop();
}