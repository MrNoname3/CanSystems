//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.
#include "connectivity.hpp"
#include "adcReader.hpp"
#include "mq135Handler.hpp"

//--- Constants ---//
static constexpr uint8_t LED_PIN                    = D8;           // Pin of the LED.
static constexpr uint8_t SPI_CS                     = D0;           // Ethernet shield SPI CS.
static constexpr uint8_t ADC_RDY                    = D2;           // ADC ready signal.
static constexpr uint8_t I2C_SDA                    = D4;
static constexpr uint8_t I2C_SCL                    = D3;

//--- Variables ---//
const char separator[] PROGMEM = "******************************************************";

//--- Driver objects ---//
DebugLedHandler debugLed(LED_PIN, HIGH);

//--- Networking ---//
Connectivity iotConn(Serial, debugLed, SPI_CS);

//--- MQTT handler objects ---//
AdcReader adcReader(iotConn, "adcreader", 100U, ADC_RDY, I2C_SDA, I2C_SCL);
Mq135Handler mq135(iotConn, "mq135", adcReader, AdcReader::Channel::AN0, 10000U);

void setup() {
  Serial.begin(MONITOR_BAUD);
  delay(1U);
  Serial.printf_P(PSTR("%s\r\nStarting...\r\n"), separator);
  iotConn.begin(Connectivity::Interface::WIFI, true);
  //adcReader.enableMqttSending(10000U);
  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), separator);
}

void loop() {
  iotConn.loop();
}