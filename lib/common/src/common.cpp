#include "common.hpp"
#if defined(__AVR_ATmega328P__)
#include <avr/boot.h>                                               /// Reading fuses.
#elif defined(ESP8266) || defined(ESP32)
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#endif

#if defined(ESP8266) || defined(ESP32)
bool FileName::isValidFileName(const char* fileName) {
  static constexpr const char* const allowedLocations[] = {
    otaFwLocation,
    mqttServerCertLocation,
    mqttServerCredLocation,
    canAlertFwLocation
  };
  for(const char* const location : allowedLocations) {
    // cppcheck-suppress useStlAlgorithm
    if(strcmp_P(fileName, location) == 0) { return true; }
  }
  return false;
}
#endif

void Build::printBuildInfo() {
#if defined(__AVR_ATmega328P__)
  Logger::get().print(F("CPP: "));
  Logger::get().println(getCppVersion());
  Logger::get().print(F("FW: "));
  Logger::get().println(getFwVersion());
  Logger::get().print(F("GIT: "));
  Logger::get().println(getGitHash(), HEX);
  Logger::get().print(F("Dirty: "));
  Logger::get().println(getGitDirty());
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
  Logger::get().printf_P(PSTR("  CPP: %u\r\n"), getCppVersion());
  Logger::get().printf_P(PSTR("  FW: %hu\r\n"), getFwVersion());
  Logger::get().printf_P(PSTR("  GIT: %x\r\n"), getGitHash());
  Logger::get().printf_P(PSTR("  Dirty: %hu\r\n"), getGitDirty());
  Logger::get().printf_P(PSTR("Reset reason: %hu\r\n"), ResetHandler::getResetReason());
#endif
}