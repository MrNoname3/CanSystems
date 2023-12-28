#include "main.hpp"

//--- Variables ---//

//--- Networking ---//
const char Connectivity::DEVICE_TYPE[] PROGMEM = "ESP8266";
Connectivity iotConn(&Serial, SPI_CS, LED, false);
Radiation radiation("radiation", RAD);

void setup() {
  Serial.begin(115200);
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
