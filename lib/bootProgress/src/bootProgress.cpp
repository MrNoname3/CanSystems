#include "bootProgress.hpp"
#if defined(ESP8266)
#include <Esp.h>                                                    /// RTC user memory access.
#elif defined(ESP32)
#include <esp_attr.h>                                               /// RTC_NOINIT_ATTR.
#endif
#if defined(ESP8266) || defined(ESP32)
#include <pgmspace.h>                                               /// PROGMEM stage names.
#else
#define PROGMEM                                                     // NOLINT(cppcoreguidelines-macro-usage) - host build has no flash strings.
#endif

BootStage BootProgress::previousStage = BootStage::Unknown;
BootStage BootProgress::currentStage = BootStage::Unknown;

namespace {
  constexpr uint32_t recordMagic = 0xB007'5747UL;                   // Tells a real record from uninitialised memory.
  constexpr uint8_t lastStage = static_cast<uint8_t>(BootStage::Running);

#if defined(ESP8266)
  constexpr uint32_t rtcOffset = 66U;                               // In 4-byte words; sits above the reconnect backoff's slot.

  bool readStage(uint8_t& stage) {
    uint32_t record[2] = { 0U };
    if(!ESP.rtcUserMemoryRead(rtcOffset, record, sizeof(record))) { return false; }
    const bool valid = (record[0] == recordMagic);
    if(valid) { stage = static_cast<uint8_t>(record[1]); }
    return valid;
  }

  void writeStage(uint8_t stage) {
    uint32_t record[2] = { recordMagic, stage };
    (void)ESP.rtcUserMemoryWrite(rtcOffset, record, sizeof(record));
  }
#elif defined(ESP32)
  RTC_NOINIT_ATTR uint32_t recordMagicRtc;                          // Survives a reset; garbage after a power cycle.
  RTC_NOINIT_ATTR uint32_t recordStageRtc;

  bool readStage(uint8_t& stage) {
    const bool valid = (recordMagicRtc == recordMagic);
    if(valid) { stage = static_cast<uint8_t>(recordStageRtc); }
    return valid;
  }

  void writeStage(uint8_t stage) {
    recordMagicRtc = recordMagic;
    recordStageRtc = stage;
  }
#else
  // Host build: plain storage, so the bookkeeping can be exercised without an RTC.
  uint32_t hostMagic = 0U;
  uint32_t hostStage = 0U;

  bool readStage(uint8_t& stage) {
    const bool valid = (hostMagic == recordMagic);
    if(valid) { stage = static_cast<uint8_t>(hostStage); }
    return valid;
  }

  void writeStage(uint8_t stage) {
    hostMagic = recordMagic;
    hostStage = stage;
  }
#endif

  // clang-format off
  constexpr const char PROGMEM unknownName[]       = "UNKNOWN";
  constexpr const char PROGMEM bannerName[]        = "BANNER";
  constexpr const char PROGMEM fileSystemName[]    = "FILE_SYSTEM";
  constexpr const char PROGMEM backoffWaitName[]   = "BACKOFF_WAIT";
  constexpr const char PROGMEM networkStartName[]  = "NETWORK_START";
  constexpr const char PROGMEM addressWaitName[]   = "ADDRESS_WAIT";
  constexpr const char PROGMEM clockSyncName[]     = "CLOCK_SYNC";
  constexpr const char PROGMEM credentialsName[]   = "CREDENTIALS";
  constexpr const char PROGMEM certificateName[]   = "CERTIFICATE";
  constexpr const char PROGMEM brokerConnectName[] = "BROKER_CONNECT";
  constexpr const char PROGMEM subscribeName[]     = "SUBSCRIBE";
  constexpr const char PROGMEM announceName[]      = "ANNOUNCE";
  constexpr const char PROGMEM runningName[]       = "RUNNING";
  // clang-format on
} // namespace

void BootProgress::begin() {
  uint8_t stored = 0U;
  // An out-of-range record is treated as nothing on record: the stage is published, so a value from
  // a firmware that knew more stages than this one must not be reported as one of ours.
  previousStage = (readStage(stored) && (stored <= lastStage)) ? static_cast<BootStage>(stored) : BootStage::Unknown;
  set(BootStage::Banner);
}

void BootProgress::set(BootStage stage) {
  currentStage = stage;
  writeStage(static_cast<uint8_t>(stage));
}

BootStage BootProgress::getPrevious() {
  return previousStage;
}

BootStage BootProgress::getCurrent() {
  return currentStage;
}

const char* BootProgress::getName(BootStage stage) {
  switch(stage) {
    case BootStage::Banner: {
      return bannerName;
    }
    case BootStage::FileSystem: {
      return fileSystemName;
    }
    case BootStage::BackoffWait: {
      return backoffWaitName;
    }
    case BootStage::NetworkStart: {
      return networkStartName;
    }
    case BootStage::AddressWait: {
      return addressWaitName;
    }
    case BootStage::ClockSync: {
      return clockSyncName;
    }
    case BootStage::Credentials: {
      return credentialsName;
    }
    case BootStage::Certificate: {
      return certificateName;
    }
    case BootStage::BrokerConnect: {
      return brokerConnectName;
    }
    case BootStage::Subscribe: {
      return subscribeName;
    }
    case BootStage::Announce: {
      return announceName;
    }
    case BootStage::Running: {
      return runningName;
    }
    default: {
      return unknownName;
    }
  }
}
