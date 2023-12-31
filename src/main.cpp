#include "main.hpp"

//--- Variables ---//

//--- Networking ---//
Connectivity iotConn(&Serial, SPI_CS, LED, false);
Radiation radiation("radiation", RAD);

void setup() {
  Serial.begin(MONITOR_BAUD);
  delay(1);
  Serial.println();
  Serial.println(F("******************************************************"));
  Serial.println(F("Starting..."));

  const bool conResult = iotConn.begin(Connectivity::Interface::ETHERNET);
  Serial.printf_P(PSTR("IOT connection:%s\r\n"), conResult ? Connectivity::OK_STATE : Connectivity::ERR_STATE);

  Serial.println(F("******************************************************"));
  Serial.println(F("Loop starting..."));
}

void loop() {
  iotConn.loop();
}
