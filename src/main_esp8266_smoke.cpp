//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.
#include "connectivity/src/connectivity.hpp"
#include "adcReader/src/adcReader.hpp"

//--- Constants ---//
static constexpr const uint8_t LED                    = D8;           // Status LED.
static constexpr const uint8_t SPI_CS                 = D0;           // Ethernet shield SPI CS.
static constexpr const uint8_t ADC_RDY                = D2;           // ADC ready signal.
static constexpr const uint8_t I2C_SDA                = D4;
static constexpr const uint8_t I2C_SCL                = D3;

//--- Variables ---//
const char separator[] PROGMEM = "******************************************************";

//--- Networking ---//
Connectivity iotConn(Serial, SPI_CS, LED, false);

//--- MQTT handler objects ---//
AdcReader adcReader(iotConn, "adcreader", 50U, ADC_RDY, I2C_SDA, I2C_SCL);

void setup() {
  Serial.printf_P(PSTR("%s\r\nStarting...\r\n"), separator);
  iotConn.begin(Connectivity::Interface::WIFI, true);
  adcReader.enableMqttSending(10000U);
  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), separator);
}

void loop() {
  iotConn.loop();
}