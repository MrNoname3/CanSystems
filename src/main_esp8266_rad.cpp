//--- Headers ---//
#include <Arduino.h>                                                /// Arduino libraries header.
#include "debugLedHandler.hpp"                                      /// Handles the debug LED.
#include "connectivity.hpp"
#include "radiation.hpp"
#include "rfHandler.hpp"

//--- Constants ---//
static constexpr uint8_t LED_PIN                    = D8;           // Pin of the LED.
static constexpr uint8_t SPI_CS                     = D0;           // Ethernet shield SPI CS.
static constexpr uint8_t RAD                        = D2;           // Radiation meter.
static constexpr uint8_t RF_RX                      = D1;           // RF transmit pin.
static constexpr uint8_t RF_TX                      = D3;           // RF receive pin.

//--- Variables ---//
const char separator[] PROGMEM = "******************************************************";

//--- Driver objects ---//
DebugLedHandler debugLed(LED_PIN, HIGH);

//--- Networking ---//
Connectivity iotConn(Serial, debugLed, SPI_CS);

//--- MQTT handler objects ---//
Radiation radiation(iotConn, "radiation", RAD);
RfHandler rfHandler(iotConn, "rf433", RF_RX, RF_TX);

void setup() {
  Serial.begin(MONITOR_BAUD);
  delay(1U);
  Serial.printf_P(PSTR("%s\r\nStarting...\r\n"), separator);
  iotConn.begin(Connectivity::Interface::ETHERNET, true);
  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), separator);
}

void loop() {
  iotConn.loop();
}