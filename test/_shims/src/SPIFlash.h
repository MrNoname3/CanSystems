#pragma once
#include <stdint.h>
#include <cassert>
#include <cstring>
#include <map>

// Default capacity: 2 × 32 KB blocks — enough for two OTA firmware images.
static constexpr uint32_t SPIFLASH_DEFAULT_CAPACITY = 65536U;

class SPIFlash {
public:
  SPIFlash(uint8_t /*slaveSelectPin*/, uint16_t id = 0U,
           uint32_t capacity = SPIFLASH_DEFAULT_CAPACITY)
    : jedecId(id), flashCapacity(capacity) {}

  [[nodiscard]] static bool initialize() { return true; }
  [[nodiscard]] static uint8_t readStatus() { return 0U; }

  [[nodiscard]] uint8_t readByte(uint32_t addr) const {
    assert(addr < flashCapacity);
    std::map<uint32_t, uint8_t>::const_iterator it = memory.find(addr);
    return (it != memory.end()) ? it->second : 0xFFU;
  }

  void readBytes(uint32_t addr, void* buf, uint16_t len) const {
    uint8_t* bytes = static_cast<uint8_t*>(buf);
    for (uint16_t i = 0U; i < len; i++) {
      bytes[i] = readByte(addr + static_cast<uint32_t>(i));
    }
  }

  void writeByte(uint32_t addr, uint8_t byt) { // NOLINT(readability-make-member-function-const)
    assert(addr < flashCapacity);
    memory[addr] = readByte(addr) & byt; // NAND: write can only clear bits (1→0); erase resets to 0xFF
  }

  void writeBytes(uint32_t addr, const void* buf, uint16_t len) {
    const uint8_t* bytes = static_cast<const uint8_t*>(buf);
    for (uint16_t i = 0U; i < len; i++) {
      writeByte(addr + static_cast<uint32_t>(i), bytes[i]);
    }
  }

  [[nodiscard]] bool busy() const { return busyFlag; }
  void setBusy(bool b) { busyFlag = b; }

  void chipErase() { memory.clear(); }

  void blockErase4K(uint32_t addr) {
    eraseRange(addr & ~static_cast<uint32_t>(0xFFFU), 4096U);
  }

  void blockErase32K(uint32_t addr) {
    eraseRange(addr & ~static_cast<uint32_t>(0x7FFFU), 32768U);
  }

  void blockErase64K(uint32_t addr) {
    eraseRange(addr & ~static_cast<uint32_t>(0xFFFFU), 65536U);
  }

  [[nodiscard]] uint16_t readDeviceId() const { return jedecId; }

  static void readUniqueId(uint8_t (&buf)[8]) { memset(buf, 0, sizeof(buf)); }

  void sleep() const {}
  void wakeup() const {}
  void end() const {}

  SPIFlash(const SPIFlash&) = delete;
  SPIFlash& operator=(const SPIFlash&) = delete;
  SPIFlash(SPIFlash&&) = delete;
  SPIFlash& operator=(SPIFlash&&) = delete;

private:
  void eraseRange(uint32_t base, uint32_t size) { // NOLINT(readability-convert-member-functions-to-static)
    std::map<uint32_t, uint8_t>::iterator it = memory.lower_bound(base);
    while (it != memory.end() && it->first < base + size) {
      it = memory.erase(it);
    }
  }

  std::map<uint32_t, uint8_t> memory;
  uint16_t jedecId;
  uint32_t flashCapacity;
  bool busyFlag = false;
};
