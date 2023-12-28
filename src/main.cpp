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
  Serial.print(F("[INIT] CPP: "));
  Serial.println(__cplusplus);
  Serial.print(F("[INIT] FW: "));
  Serial.println(FPSTR(SW_VERSION));
  Serial.printf_P(PSTR("[INIT] Git hash: %x\r\n"), GIT_COMMIT_HASH);
  Serial.printf_P(PSTR("[INIT] Internal VCC: %humV\r\n"), ESP.getVcc());

  const bool conResult = iotConn.begin(Connectivity::Interface::ETHERNET);
  Serial.printf_P(PSTR("IOT connection:%s\r\n"), conResult ? Connectivity::OK_STATE : Connectivity::ERR_STATE);

  Serial.println(F("******************************************************"));
  Serial.println(F("Loop starting..."));
}

void loop() {
  iotConn.loop();
}
