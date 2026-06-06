#include "common.hpp"
#if defined(__AVR_ATmega328P__)
#include <avr/boot.h>                                               /// Reading fuses.
#elif defined(ESP8266) || defined(ESP32)
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include <time.h>                                                   /// UTC time retrieval/formatting (NTP-backed clock).
#endif

#if defined(ESP8266) || defined(ESP32)
namespace {
/// @brief Returns the current time as a validated UTC `tm`, or nullptr if the clock is unset.
const tm* utcNow() {
  const time_t currentTime = time(nullptr);
  if(currentTime == -1) { return nullptr; }           // Clock not set yet (NTP not synced).
  return gmtime(&currentTime);                        // Always UTC, independent of any TZ setting.
}
}  // namespace

bool Time::getIsoUtcString(char* buf, size_t bufSize) {  // NOLINT(readability-convert-member-functions-to-static) declared static in the header (out-of-line definition)
  const tm* utc = utcNow();
  if(utc == nullptr) { return false; }
  const size_t formattedSize = strftime(buf, bufSize, "%Y-%m-%dT%H:%M:%SZ", utc);
  return (formattedSize > 0U && formattedSize < bufSize);
}

bool Time::getUtcFileStamp(char* buf, size_t bufSize) {  // NOLINT(readability-convert-member-functions-to-static) declared static in the header (out-of-line definition)
  const tm* utc = utcNow();
  if(utc == nullptr) { return false; }
  const size_t formattedSize = strftime(buf, bufSize, "%Y%m%d_%H%M%SZ", utc);
  return (formattedSize > 0U && formattedSize < bufSize);
}
#endif

// Single implementation shared by the ESP and native-test builds: strcmp_P collapses to strcmp
// off-target (see the pgmspace shim), so there is only one copy to keep in sync with the list.
#if defined(ESP8266) || defined(ESP32) || defined(NATIVE_TEST)
bool FileName::isValidFileName(const char* fileName) {
  static constexpr const char* const allowedLocations[] = {
    otaFwLocation,
    mqttServerCertLocation,
    mqttServerCredLocation,
    canAlertFwLocation,
    tubeConfigLocation
  };
  // The fixed allow-list is clearer as a raw loop; any_of would not shrink it. (strcmp_P keeps
  // clang-tidy quiet on ESP, but the host build sees plain strcmp, hence the explicit suppressions.)
  // cppcheck-suppress useStlAlgorithm
  for(const char* const location : allowedLocations) {  // NOLINT(readability-use-anyofallof)
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