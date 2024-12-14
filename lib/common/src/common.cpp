#include "common.hpp"

#if defined(ESP8266) || defined(ESP32)
const char Str::OK_STR[] PROGMEM                = "[OK]";
const char Str::ERR_STR[] PROGMEM               = "[ERR]";
#endif