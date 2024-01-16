#include "main.hpp"

//--- Variables ---//
const char separator[] PROGMEM = "******************************************************";

//--- Networking ---//
Connectivity iotConn(Serial, SPI_CS, LED, false);
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
