#pragma once
// Host stand-in for the serial flash chip, driven through the SPI byte stream, so the real
// SPIFlash driver can be exercised natively. It decodes the commands that driver issues -
// status, write enable, JEDEC id, unique id, program, read, erase - and keeps a 64 KB memory
// that starts erased. A program wraps inside its page the way the chip's page register does,
// and every transaction is recorded as well: the address and the length the driver chose are
// what says where it split a write at a page boundary.
//
// Reached from SPI.h only while the chip-select pin the test attached it to is LOW, so the
// MCP2515 model on the same bus is untouched by its presence.
#include <stdint.h>
#include <string.h>
#include <vector>

/// @brief Serial flash stand-in: memory, command decoding, and what the driver asked of it.
class SpiFlashModel final {
public:
  static constexpr uint32_t memorySize = 1UL << 16U;              // 64 KB: more than the largest image stored here.
  static constexpr uint8_t erasedByte = 0xFFU;                    // What an erased cell reads as.
  static constexpr uint8_t statusBusy = 0x01U;                    // Status register bit 0 (WIP).

  /// @brief One page-program transaction the driver issued.
  struct Program {
    uint32_t address = 0U;                                        // Address the transaction opened with.
    uint16_t length = 0U;                                         // Data bytes it carried.
  };

  SpiFlashModel() { reset(); }

  /// @brief Erases the memory and forgets every recorded transaction.
  void reset() {
    memory.assign(memorySize, erasedByte);
    programs.clear();
    byteIndex = 0U;
    command = 0U;
    address = 0U;
    writeEnabled = false;
    busyReads = 0U;
    notResponding = false;
  }

  // ---- test controls ----

  /// @brief JEDEC id answered to CMD_READ_ID, as the driver reads it: manufacturer then device.
  void setJedecId(uint16_t id) { jedecId = id; }
  /// @brief Density code (byte 3 of the id); the driver reads capacity as 2^code bytes.
  void setDensityCode(uint8_t code) { densityCode = code; }
  /// @brief Unique id answered to CMD_READ_MAC.
  void setUniqueId(const uint8_t (&id)[8]) { memcpy(uniqueId, id, sizeof(uniqueId)); }
  /// @brief Report a write in progress for this many status reads, then come ready.
  void setBusyReads(uint16_t count) { busyReads = count; }
  /// @brief Drive every status read all ones, as an absent chip leaves the line.
  void setNotResponding(bool state) { notResponding = state; }

  // ---- observation ----

  /// @brief The page-program transactions issued since the last reset(), in order.
  [[nodiscard]] const std::vector<Program>& getPrograms() const { return programs; }
  /// @brief Byte at a flash address.
  [[nodiscard]] uint8_t byteAt(uint32_t flashAddress) const {
    return (flashAddress < memorySize) ? memory[flashAddress] : erasedByte;
  }
  /// @brief Whether a range holds exactly these bytes.
  [[nodiscard]] bool holds(uint32_t flashAddress, const uint8_t* expected, uint16_t length) const {
    if((expected == nullptr) || ((flashAddress + length) > memorySize)) { return false; }
    return memcmp(&memory[flashAddress], expected, length) == 0;
  }
  /// @brief Whether the write-enable latch is set, as it is between WREN and the write it arms.
  [[nodiscard]] bool isWriteEnabled() const { return writeEnabled; }

  // ---- SPI byte stream ----

  /// @brief Starts a new transaction; the next transferred byte is the command.
  void beginMessage() {
    byteIndex = 0U;
    command = 0U;
    address = 0U;
  }

  /// @brief Feeds one byte of the stream to the chip and returns the byte it drives back.
  uint8_t transfer(uint8_t out) {
    const uint8_t result = decode(out);
    byteIndex++;
    return result;
  }

private:
  static constexpr uint8_t cmdWriteEnable = 0x06U;
  static constexpr uint8_t cmdErase4K = 0x20U;
  static constexpr uint8_t cmdErase32K = 0x52U;
  static constexpr uint8_t cmdErase64K = 0xD8U;
  static constexpr uint8_t cmdEraseChip = 0x60U;
  static constexpr uint8_t cmdStatusRead = 0x05U;
  static constexpr uint8_t cmdStatusWrite = 0x01U;
  static constexpr uint8_t cmdArrayRead = 0x0BU;                  // One dummy byte after the address.
  static constexpr uint8_t cmdArrayReadLf = 0x03U;
  static constexpr uint8_t cmdProgram = 0x02U;
  static constexpr uint8_t cmdReadId = 0x9FU;
  static constexpr uint8_t cmdReadMac = 0x4BU;                    // Four dummy bytes, then the id.

  /// @brief Status the chip drives while the driver polls it.
  [[nodiscard]] uint8_t status() {
    if(notResponding) { return 0xFFU; }
    if(busyReads > 0U) {
      busyReads--;
      return statusBusy;
    }
    return 0x00U;
  }

  /// @brief Erases the block of `size` bytes the current address falls in.
  void erase(uint32_t size) {
    const uint32_t base = (address / size) * size;
    if(base < memorySize) {
      memset(&memory[base], erasedByte, (size < (memorySize - base)) ? size : (memorySize - base));
    }
  }

  uint8_t decode(uint8_t out) {
    if(byteIndex == 0U) {
      command = out;
      if(command == cmdWriteEnable) { writeEnabled = true; }
      if(command == cmdEraseChip) {
        memory.assign(memorySize, erasedByte);
        writeEnabled = false;
      }
      return 0U;
    }
    // Bytes 1-3 carry the address for every command that takes one.
    const bool takesAddress = (command == cmdProgram) || (command == cmdArrayRead) ||
                              (command == cmdArrayReadLf) || (command == cmdErase4K) ||
                              (command == cmdErase32K) || (command == cmdErase64K);
    if(takesAddress && (byteIndex <= 3U)) {
      address = (address << 8U) | out;
      if(byteIndex == 3U) {
        if(command == cmdErase4K) { erase(4096U); }
        if(command == cmdErase32K) { erase(32768U); }
        if(command == cmdErase64K) { erase(65536U); }
        if(command == cmdProgram) { programs.push_back(Program{ address, 0U }); }
      }
      return 0U;
    }
    switch(command) {
      case cmdStatusRead: {
        return status();
      }
      case cmdReadId: {
        // Manufacturer, device, density - the first two are what the driver compares.
        const uint8_t idBytes[3] = { static_cast<uint8_t>(jedecId >> 8U), static_cast<uint8_t>(jedecId), densityCode };
        return (byteIndex <= 3U) ? idBytes[byteIndex - 1U] : 0U;
      }
      case cmdReadMac: {
        // Four dummy bytes precede the id.
        const uint8_t offset = static_cast<uint8_t>(byteIndex - 1U);
        return (offset >= 4U && offset < 12U) ? uniqueId[offset - 4U] : 0U;
      }
      case cmdProgram: {
        // The address wraps inside its own 256-byte page, as the chip's page register does: a
        // program that runs past the page end comes back to its start and overwrites what it
        // already took. That is what a driver splitting at 256 from the start of the write,
        // rather than at the next page boundary, runs into.
        const uint32_t target = (address & ~0xFFUL) | ((address + programs.back().length) & 0xFFUL);
        // A program only sets bits to zero, as the cells themselves do; the caller is the one
        // that has to erase first, and a test asserting on the memory sees it if it did not.
        if(target < memorySize) { memory[target] &= out; }
        programs.back().length++;
        writeEnabled = false;
        return 0U;
      }
      case cmdArrayReadLf: {
        return byteAt(address + (byteIndex - 4U));
      }
      case cmdArrayRead: {
        // Byte 4 is the dummy the fast read takes before the data.
        return (byteIndex == 4U) ? 0U : byteAt(address + (byteIndex - 5U));
      }
      case cmdStatusWrite:
      default: {
        return 0U;
      }
    }
  }

  std::vector<uint8_t> memory;                                    // The flash array, erased at reset().
  std::vector<Program> programs;                                  // Page-program transactions, in order.
  uint8_t uniqueId[8] = {};                                       // Answered to CMD_READ_MAC.
  uint16_t jedecId = 0xEF30U;                                     // Winbond W25X40CL, the part this project uses.
  uint8_t densityCode = 0x13U;                                    // 2^19 bytes = 512 KB.
  uint8_t command = 0U;                                           // Command byte of the transaction in progress.
  uint32_t address = 0U;                                          // Address it carried, once all three bytes arrived.
  uint16_t byteIndex = 0U;                                        // Position in the transaction.
  uint16_t busyReads = 0U;                                        // Status reads still to answer busy.
  bool writeEnabled = false;                                      // Write-enable latch.
  bool notResponding = false;                                     // Drive every status read all ones.
};
