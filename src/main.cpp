#include "main.hpp"

//--- Variables ---//

//--- Debug LED ---//
Ticker ticker;


//--- Networking ---//
const char Connectivity::DEVICE_TOPIC[] PROGMEM = "test";
Connectivity iotConn(&Serial, SPI_CS);

Radiation radiation("radiation", RAD);

void setup() {
  wdt_disable();                                          // Disables the SW watchdog and enables the HW watchdog -> ~8400ms
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  delay(1);
  ticker.attach(0.2, tick);
  Serial.println();
  Serial.println(F("******************************************************"));
  Serial.println(F("Starting..."));
  Serial.print(F("[INIT] CPP: "));
  Serial.println(__cplusplus);
  Serial.print(F("[INIT] FW: "));
  Serial.println(FPSTR(SW_VERSION));
  Serial.print(F("[INIT] Git hash: "));
  Serial.println(F(GIT_COMMIT_HASH));
  Serial.printf_P(PSTR("[INIT] Internal VCC: %humV\r\n"), ESP.getVcc());

  const bool conResult = iotConn.begin(Connectivity::Interface::ETHERNET);
  Serial.printf_P(PSTR("IOT connection:%s\r\n"), conResult ? Connectivity::OK_STATE : Connectivity::ERR_STATE);

  radiation.begin();

  Serial.println(F("******************************************************"));
  Serial.println(F("Loop starting..."));
  ticker.detach();
  LED_L;
  wdt_reset();
}

void loop() {
  yield();
  iotConn.loop();
  radiation.loop();
  wdt_reset();
}

void tick() { LED_T; }

void RestartESP() {
  Serial.println(F("Restarting..."));
  Serial.flush();                                     // Sends out data from serial buffer, before reset.
  ESP.restart();
  delay(10000);                                       // Prevent doing anything before restart.
}
