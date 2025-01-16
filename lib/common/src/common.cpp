#include "common.hpp"
#if defined(__AVR_ATmega328P__)
#include <avr/boot.h>                                               /// Reading fuses.
#elif defined(ESP8266) || defined(ESP32)
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include <pgmspace.h>                                               /// Provides PROGMEM support for storing data in flash memory.
#endif

#if defined(ESP8266) || defined(ESP32)
const char Str::okStr[] PROGMEM                = "[OK]";
const char Str::errStr[] PROGMEM               = "[ERR]";
const char Str::sectionSeparator[] PROGMEM     = "*************************************************";
#endif

void Build::printBuildInfo() {
#if defined(__AVR_ATmega328P__)
  Logger::get().print(F("CPP: "));
  Logger::get().println(Build::getCppVersion());
  Logger::get().print(F("FW: "));
  Logger::get().println(Build::getFwVersion());
  Logger::get().print(F("GIT: "));
  Logger::get().println(Build::getGitHash(), HEX);
  Logger::get().print(F("Dirty: "));
  Logger::get().println(Build::getGitDirty());
  Logger::get().print(F("Fuses: "));
  Logger::get().print(boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS), HEX);
  Logger::get().print(Str::getSpacerStr());
  Logger::get().print(boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS), HEX);
  Logger::get().print(Str::getSpacerStr());
  Logger::get().print(boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS), HEX);
  Logger::get().print(Str::getSpacerStr());
  Logger::get().println(boot_lock_fuse_bits_get(GET_LOCK_BITS), HEX);
#elif defined(ESP8266) || defined(ESP32)
  Logger::get().printf_P(PSTR("Build info:\r\n"));
  Logger::get().printf_P(PSTR("  CPP: %u\r\n"), Build::getCppVersion());
  Logger::get().printf_P(PSTR("  FW: %hu\r\n"), Build::getFwVersion());
  Logger::get().printf_P(PSTR("  GIT: %x\r\n"), Build::getGitHash());
  Logger::get().printf_P(PSTR("  Dirty: %hu\r\n"), Build::getGitDirty());
  Logger::get().printf_P(PSTR("Reset reason: %hu\r\n"), ResetHandler::getResetReason());
#endif
}

#if defined(ESP8266) || defined(ESP32)
const char FileName::tempFileLocation[] PROGMEM           = "/temp.tmp";
const char FileName::otaFwLocation[] PROGMEM              = "/espFirmware.bin";
const char FileName::extOtaFwLocation[] PROGMEM           = "/%sFirmware.bin";
const char FileName::wifiConfigLocation[] PROGMEM         = "/config/wifi.json";
const char FileName::wifiTempConfigLocation[] PROGMEM     = "/wifi.tmp";
const char FileName::mqttServerCertLocation[] PROGMEM     = "/config/mosq-ca.crt";
const char FileName::mqttServerCredLocation[] PROGMEM     = "/config/server.json";
#endif