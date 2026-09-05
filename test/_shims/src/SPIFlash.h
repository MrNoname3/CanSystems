#pragma once
// Native-test shim for the W25Q64 SPI flash driver: an address -> byte map where a write only
// clears bits and an erase restores 0xFF. The real class routes every operation through
// command() -> waitUntilReady(), which gives up after busyTimeoutMs and reports false; the three
// setFail*() hooks reproduce that refusal per operation kind, so each OTA failure branch can be
// reached on its own.
#include <stdint.h>
#include <cassert>
#include <cstring>
#include <map>

// Default capacity: 2 × 32 KB blocks — enough for two OTA firmware images.
static constexpr uint32_t SPIFLASH_DEFAULT_CAPACITY = 65536U;

class SPIFlash {
public:
  SPIFlash(uint8_t /*slaveSelectPin*/, uint16_t id = 0U, uint32_t capacity = SPIFLASH_DEFAULT_CAPACITY) :
    jedecId(id),
    flashCapacity(capacity) {}

  [[nodiscard]] static bool initialize() { return true; }
  [[nodiscard]] static uint8_t readStatus() { return 0U; }

  [[nodiscard]] uint32_t capacity() const { return flashCapacity; }

  [[nodiscard]] uint8_t readByte(uint32_t addr) const {
    assert(addr < flashCapacity);
    std::map<uint32_t, uint8_t>::const_iterator it = memory.find(addr);
    return (it != memory.end()) ? it->second : 0xFFU;
  }

  bool readBytes(uint32_t addr, void* buf, uint16_t len) const {
    uint8_t* bytes = static_cast<uint8_t*>(buf);
    if(failRead) {
      memset(buf, 0, len);               // the real class zero-fills what it could not read
      return false;
    }
    for(uint16_t i = 0U; i < len; i++) {
      bytes[i] = readByte(addr + static_cast<uint32_t>(i));
    }
    return true;
  }

  bool writeByte(uint32_t addr, uint8_t byt) { // NOLINT(readability-make-member-function-const)
    if(failWrite) { return false; }      // NOLINT(readability-simplify-boolean-expr) guard, not a boolean return: the write below still has to run
    programCommands++;
    program(addr, byt);
    return true;
  }

  bool writeBytes(uint32_t addr, const void* buf, uint16_t len) {
    if(failWrite) { return false; }      // NOLINT(readability-simplify-boolean-expr)
    // One command however long the run is, as the chip charges it: the program cycle is what
    // costs the time, and a page program runs one of them for the whole run.
    programCommands++;
    const uint8_t* bytes = static_cast<const uint8_t*>(buf);
    for(uint16_t i = 0U; i < len; i++) {
      program(addr + static_cast<uint32_t>(i), bytes[i]);
    }
    return true;
  }

  /// @brief Program commands issued since the last clear, which is what a real chip charges for.
  [[nodiscard]] uint32_t getProgramCommands() const { return programCommands; }
  void clearProgramCommands() { programCommands = 0U; }

  [[nodiscard]] bool busy() const { return busyFlag; }
  void setBusy(bool b) { busyFlag = b; }
  void setFailWrite(bool fail) { failWrite = fail; }   // test hook: writes report a chip that never came ready
  void setFailRead(bool fail) { failRead = fail; }     // test hook: reads report a chip that never came ready
  void setFailErase(bool fail) { failErase = fail; }   // test hook: erases report a chip that never came ready

  bool chipErase() { // NOLINT(readability-make-member-function-const) clears the instance's map
    if(failErase) { return false; }   // NOLINT(readability-simplify-boolean-expr) guard, not a boolean return
    memory.clear();
    return true;
  }

  bool blockErase4K(uint32_t addr) {
    if(failErase) { return false; }
    eraseRange(addr & ~static_cast<uint32_t>(0xFFFU), 4096U);
    return true;
  }

  bool blockErase32K(uint32_t addr) {
    if(failErase) { return false; }
    eraseRange(addr & ~static_cast<uint32_t>(0x7FFFU), 32768U);
    return true;
  }

  bool blockErase64K(uint32_t addr) {
    if(failErase) { return false; }
    eraseRange(addr & ~static_cast<uint32_t>(0xFFFFU), 65536U);
    return true;
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
  /// @brief Stores one byte the way NOR does: a write can only clear bits, an erase resets them.
  void program(uint32_t addr, uint8_t byt) { // NOLINT(readability-make-member-function-const)
    assert(addr < flashCapacity);
    memory[addr] = readByte(addr) & byt;
  }

  void eraseRange(uint32_t base, uint32_t size) { // NOLINT(readability-convert-member-functions-to-static)
    std::map<uint32_t, uint8_t>::iterator it = memory.lower_bound(base);
    while(it != memory.end() && it->first < base + size) {
      it = memory.erase(it);
    }
  }

  std::map<uint32_t, uint8_t> memory;
  uint16_t jedecId;
  uint32_t flashCapacity;
  bool busyFlag = false;
  bool failWrite = false;
  bool failRead = false;
  bool failErase = false;
  uint32_t programCommands = 0U;
};
