#include "main.hpp"

//--- Variables ---//

//--- Networking ---//
Connectivity iotConn(&Serial, SPI_CS, LED, false);
Radiation radiation(&iotConn, "radiation", RAD);
RfHandler rfHandler(&iotConn, "rf433", RF_RX, RF_TX);

void setup() {
  Serial.begin(MONITOR_BAUD);
  delay(1);
  Serial.println();
  Serial.println(F("******************************************************"));
  Serial.println(F("Starting..."));
  iotConn.begin(Connectivity::Interface::ETHERNET, true);
  Serial.println(F("******************************************************"));
  Serial.println(F("Loop starting..."));
}

void loop() {
  iotConn.loop();
}
