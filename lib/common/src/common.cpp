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

void Build::printBuildInfo(HardwareSerial& serial) {
#if defined(__AVR_ATmega328P__)
  serial.print(F("CPP: "));
  serial.println(Build::getCppVersion());
  serial.print(F("FW: "));
  serial.println(Build::getFwVersion());
  serial.print(F("GIT: "));
  serial.println(Build::getGitHash(), HEX);
  serial.print(F("Dirty: "));
  serial.println(Build::getGitDirty());
  serial.print(F("Fuses: "));
  serial.print(boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS), HEX);
  serial.print(Str::getSpacerStr());
  serial.print(boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS), HEX);
  serial.print(Str::getSpacerStr());
  serial.print(boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS), HEX);
  serial.print(Str::getSpacerStr());
  serial.println(boot_lock_fuse_bits_get(GET_LOCK_BITS), HEX);
#elif defined(ESP8266) || defined(ESP32)
  serial.printf_P(PSTR("Build info:\r\n"));
  serial.printf_P(PSTR("  CPP: %u\r\n"), Build::getCppVersion());
  serial.printf_P(PSTR("  FW: %hu\r\n"), Build::getFwVersion());
  serial.printf_P(PSTR("  GIT: %x\r\n"), Build::getGitHash());
  serial.printf_P(PSTR("  Dirty: %hu\r\n"), Build::getGitDirty());
  serial.printf_P(PSTR("Reset reason: %hu\r\n"), ResetHandler::getResetReason());
#endif
}

#if defined(ESP8266) || defined(ESP32)
const char FileName::tempFileLocation[] PROGMEM           = "/temp.tmp";
const char FileName::otaFwLocation[] PROGMEM              = "/espFirmware.bin";
const char FileName::extOtaFwLocation[] PROGMEM           = "/extFirmware.bin";
const char FileName::wifiConfigLocation[] PROGMEM         = "/config/wifi.json";
const char FileName::wifiTempConfigLocation[] PROGMEM     = "/wifi.tmp";
const char FileName::mqttServerCertLocation[] PROGMEM     = "/config/mosq-ca.crt";
const char FileName::mqttServerCredLocation[] PROGMEM     = "/config/server.json";
#endif