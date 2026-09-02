#include "rtcStore.hpp"
#if defined(ESP8266)
#include <Esp.h>                                                    /// RTC user memory access.
#elif defined(ESP32)
#include <esp_attr.h>                                               /// RTC_NOINIT_ATTR.
#endif

namespace {
  constexpr uint32_t recordMagic = 0x52544301UL;                    // Tells a real record from uninitialised memory.
  constexpr uint8_t magicWord = 0U;                                 // Record layout: magic, then the written mask, then one word per slot.
  constexpr uint8_t maskWord = 1U;
  constexpr uint8_t firstValueWord = 2U;
  constexpr uint8_t recordWords = firstValueWord + RtcStore::slotCount;

#if defined(ESP8266)
  constexpr uint32_t rtcOffset = 64U;                               // In 4-byte words.

  void loadRecord(uint32_t* record) {
    if(!ESP.rtcUserMemoryRead(rtcOffset, record, recordWords * sizeof(uint32_t))) {
      record[magicWord] = 0U;                                       // A read that failed is no better than no record.
    }
  }

  void saveRecord(const uint32_t* record) {
    // The framework takes a non-const pointer even though it only reads through it.
    (void)ESP.rtcUserMemoryWrite(rtcOffset, const_cast<uint32_t*>(record), recordWords * sizeof(uint32_t));
  }
#elif defined(ESP32)
  RTC_NOINIT_ATTR uint32_t rtcRecord[recordWords];                  // Survives a reset; garbage after a power cycle.

  void loadRecord(uint32_t* record) {
    for(uint8_t i = 0U; i < recordWords; i++) { record[i] = rtcRecord[i]; }
  }

  void saveRecord(const uint32_t* record) {
    for(uint8_t i = 0U; i < recordWords; i++) { rtcRecord[i] = record[i]; }
  }
#else
  uint32_t hostRecord[recordWords] = { 0U };                        // Host build: plain storage, so the users stay testable.

  void loadRecord(uint32_t* record) {
    for(uint8_t i = 0U; i < recordWords; i++) { record[i] = hostRecord[i]; }
  }

  void saveRecord(const uint32_t* record) {
    for(uint8_t i = 0U; i < recordWords; i++) { hostRecord[i] = record[i]; }
  }
#endif

  /// @brief Reads the record, replacing anything that is not one with an empty one.
  void readOrEmpty(uint32_t* record) {
    loadRecord(record);
    if(record[magicWord] != recordMagic) {
      record[magicWord] = recordMagic;
      record[maskWord] = 0U;
      for(uint8_t i = 0U; i < RtcStore::slotCount; i++) { record[firstValueWord + i] = 0U; }
    }
  }

  constexpr uint32_t slotBit(RtcStore::Slot slot) {
    return static_cast<uint32_t>(1UL) << static_cast<uint8_t>(slot);
  }
} // namespace

bool RtcStore::read(Slot slot, uint8_t& value) {
  uint32_t record[recordWords] = { 0U };
  readOrEmpty(record);
  if((record[maskWord] & slotBit(slot)) == 0U) { return false; }
  value = static_cast<uint8_t>(record[firstValueWord + static_cast<uint8_t>(slot)]);
  return true;
}

void RtcStore::write(Slot slot, uint8_t value) {
  uint32_t record[recordWords] = { 0U };
  readOrEmpty(record);
  record[maskWord] |= slotBit(slot);
  record[firstValueWord + static_cast<uint8_t>(slot)] = value;
  saveRecord(record);
}
