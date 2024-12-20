#include "common.hpp"

#if defined(ESP8266) || defined(ESP32)
const char Str::okStr[] PROGMEM                = "[OK]";
const char Str::errStr[] PROGMEM               = "[ERR]";
const char Str::sectionSeparator[] PROGMEM     = "*************************************************";

const char FileName::tempFileLocation[] PROGMEM           = "/temp.tmp";
const char FileName::otaFwLocation[] PROGMEM              = "/espFirmware.bin";
const char FileName::extOtaFwLocation[] PROGMEM           = "/extFirmware.bin";
const char FileName::wifiConfigLocation[] PROGMEM         = "/config/wifi.json";
const char FileName::wifiTempConfigLocation[] PROGMEM     = "/wifi.tmp";
#endif