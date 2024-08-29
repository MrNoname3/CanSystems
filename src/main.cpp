//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.

//--- Variables ---//
const char separator[] PROGMEM = "******************************************************";

void setup() {
  Serial.printf_P(PSTR("%s\r\nStarting...\r\n"), separator);

  Serial.printf_P(PSTR("%s\r\nLoop starting...\r\n"), separator);
}

void loop() {

}