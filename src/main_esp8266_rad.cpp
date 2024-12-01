//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.
#include "connectivity.hpp"
#include "radiation.hpp"
#include "rfHandler.hpp"

//--- Constants ---//
static constexpr const uint8_t LED                    = D8;           // Status LED.
static constexpr const uint8_t SPI_CS                 = D0;           // Ethernet shield SPI CS.
static constexpr const uint8_t RAD                    = D2;           // Radiation meter.
static constexpr const uint8_t RF_RX                  = D1;           // RF transmit pin.
static constexpr const uint8_t RF_TX                  = D3;           // RF receive pin.

//--- Variables ---//
const char separator[] PROGMEM = "******************************************************";

//--- Networking ---//
Connectivity iotConn(Serial, SPI_CS, LED, false);

//--- MQTT handler objects ---//
Radiation radiation(iotConn, "radiation", RAD);
RfHandler rfHandler(iotConn, "rf433", RF_RX, RF_TX);

void setup() {
  Serial.printf_P(PSTR("%s\r\nStarting...\r\n"), separator);
  iotConn.begin(Connectivity::Interface::ETHERNET, true);
  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), separator);
}

void loop() {
  iotConn.loop();
}