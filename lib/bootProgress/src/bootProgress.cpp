#include "bootProgress.hpp"
#include "rtcStore.hpp"                                             /// Storage that survives a reset.
#if defined(ESP8266) || defined(ESP32)
#include <pgmspace.h>                                               /// PROGMEM stage names.
#else
#define PROGMEM                                                     // NOLINT(cppcoreguidelines-macro-usage) - the host build has no flash strings.
#endif

BootStage BootProgress::previousStage = BootStage::Unknown;
BootStage BootProgress::currentStage = BootStage::Unknown;

namespace {
  constexpr uint8_t lastStage = static_cast<uint8_t>(BootStage::Running);

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
  previousStage = (RtcStore::read(RtcStore::Slot::BootStage, stored) && (stored <= lastStage)) ? static_cast<BootStage>(stored) : BootStage::Unknown;
  set(BootStage::Banner);
}

void BootProgress::set(BootStage stage) {
  currentStage = stage;
  RtcStore::write(RtcStore::Slot::BootStage, static_cast<uint8_t>(stage));
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
