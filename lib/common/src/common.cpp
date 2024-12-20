#include "common.hpp"

#if defined(ESP8266) || defined(ESP32)
const char Str::okStr[] PROGMEM                = "[OK]";
const char Str::errStr[] PROGMEM               = "[ERR]";
const char Str::sectionSeparator[] PROGMEM     = "*************************************************";
#endif