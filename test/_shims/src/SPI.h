#pragma once
// Host SPI stand-in wired to a register-level MCP2515 model.
//
// The MCP2515 driver only ever reaches the chip through four SPI commands (READ, WRITE,
// BIT MODIFY, RESET), so a 128-byte register file plus that command decoding is enough to
// exercise it natively. SPIFlash - the other SPI device in this project - has its own fake
// (SPIFlash.h) and never reaches this file, so the model can stay MCP2515-specific.
//
// Transaction boundaries come from beginTransaction()/endTransaction(), which the driver
// brackets every register access with; the chip-select line is therefore not modelled.

#include <stdint.h>
#include <string.h>
#include "Arduino.h"

enum : uint8_t {
  MSBFIRST = 1U,
  SPI_MODE0 = 0U
};

/// @brief Clock, bit order and data mode carry no observable behaviour on the host.
class SPISettings final {
public:
  SPISettings() = default;
  SPISettings(uint32_t /*clock*/, uint8_t /*bitOrder*/, uint8_t /*dataMode*/) {}
};

/// @brief Register-level MCP2515 stand-in driven through the SPI byte stream.
class Mcp2515Model final {
public:
  static constexpr uint8_t registerCount = 128U;
  static constexpr uint8_t flagIde = 0x08U;                       // SIDL: extended identifier.
  static constexpr uint8_t flagRtr = 0x40U;                       // DLC: remote transmission request.
  static constexpr uint8_t flagTxReq = 0x08U;                     // TXBnCTRL: transmit request pending.

  /// @brief How the modelled controller finishes a frame handed to a transmit buffer.
  enum class TxBehaviour : uint8_t {
    Completes,   // The frame goes out: TXREQ clears by the time the buffer is polled.
    NeverEnds    // Nothing on the bus takes the frame: TXREQ stays set until software clears it.
  };

  /// @brief Clears every register, as the RESET command does.
  void reset() {
    memset(registers, 0, sizeof(registers));
    command = 0U;
    address = 0U;
    byteIndex = 0U;
    bitModifyMask = 0U;
    txBehaviour = TxBehaviour::Completes;
    pollDurationMs = 0U;
  }

  void setTxBehaviour(TxBehaviour behaviour) { txBehaviour = behaviour; }

  /// @brief Advances the fake clock by this many milliseconds whenever a transmit buffer's
  /// control register is polled, standing in for the time a real poll costs. 0 leaves it alone.
  void setPollDurationMs(uint32_t milliseconds) { pollDurationMs = milliseconds; }

  /// @brief Direct register access for test setup and assertions.
  [[nodiscard]] uint8_t& reg(uint8_t regAddress) { return registers[regAddress & 0x7FU]; }

  /// @brief Register address of receive buffer `buffer`'s control register.
  [[nodiscard]] static constexpr uint8_t rxCtrl(uint8_t buffer) { return static_cast<uint8_t>(0x60U + buffer * 0x10U); }
  /// @brief Register address of transmit buffer `buffer`'s control register.
  [[nodiscard]] static constexpr uint8_t txCtrl(uint8_t buffer) { return static_cast<uint8_t>(0x30U + buffer * 0x10U); }
  /// @brief Interrupt flag bit of receive buffer `buffer` in CANINTF.
  [[nodiscard]] static constexpr uint8_t rxIntFlag(uint8_t buffer) { return static_cast<uint8_t>(0x01U << buffer); }

  static constexpr uint8_t regCanIntf = 0x2CU;                    // CANINTF.

  /// @brief Loads an extended frame into receive buffer `buffer` and raises its interrupt flag.
  /// @param buffer Receive buffer index (0 or 1).
  /// @param extId 29-bit extended identifier.
  /// @param data Payload bytes; may be nullptr when `dlc` is 0.
  /// @param dlc Payload length (0-8).
  /// @param rtr Marks the frame as a remote transmission request.
  void deliverExtendedFrame(uint8_t buffer, uint32_t extId, const uint8_t* data, uint8_t dlc, bool rtr = false) {
    const uint32_t idA = (extId >> 18U) & 0x7FFU;
    const uint32_t idB = extId & 0x3FFFFU;
    const uint8_t base = static_cast<uint8_t>(rxCtrl(buffer) + 1U);  // SIDH, SIDL, EID8, EID0, DLC, D0..
    registers[base] = static_cast<uint8_t>((idA >> 3U) & 0xFFU);
    registers[base + 1U] = static_cast<uint8_t>(((idA & 0x07U) << 5U) | flagIde | ((idB >> 16U) & 0x03U));
    registers[base + 2U] = static_cast<uint8_t>((idB >> 8U) & 0xFFU);
    registers[base + 3U] = static_cast<uint8_t>(idB & 0xFFU);
    registers[base + 4U] = static_cast<uint8_t>((dlc & 0x0FU) | (rtr ? flagRtr : 0x00U));
    for(uint8_t i = 0U; i < dlc; ++i) {
      registers[base + 5U + i] = (data != nullptr) ? data[i] : 0U;
    }
    registers[regCanIntf] |= rxIntFlag(buffer);
  }

  /// @brief Starts a new SPI message; the next transferred byte is the command.
  void beginMessage() { byteIndex = 0U; }

  /// @brief Feeds one byte of the SPI stream to the model and returns the byte it drives back.
  uint8_t transfer(uint8_t out) {
    uint8_t result = 0U;
    if(byteIndex == 0U) {
      command = out;
      if(command == cmdReset) { reset(); }
    } else if(byteIndex == 1U) {
      address = static_cast<uint8_t>(out & 0x7FU);
    } else {
      const uint8_t offset = static_cast<uint8_t>(byteIndex - 2U);
      switch(command) {
        case cmdRead: {
          const uint8_t target = static_cast<uint8_t>(address + offset) & 0x7FU;
          applyTxBehaviour(target);
          result = registers[target];
        } break;
        case cmdWrite: {
          registers[static_cast<uint8_t>(address + offset) & 0x7FU] = out;
        } break;
        case cmdBitModify: {
          if(offset == 0U) {
            bitModifyMask = out;
          } else if(offset == 1U) {
            uint8_t& target = registers[address];
            target = static_cast<uint8_t>((target & static_cast<uint8_t>(~bitModifyMask)) | (out & bitModifyMask));
          }
        } break;
        default: {
        } break;
      }
    }
    ++byteIndex;
    return result;
  }

private:
  /// @brief Runs the transmit behaviour when a transmit buffer's control register is polled.
  void applyTxBehaviour(uint8_t target) {
    if((target != txCtrl(0U)) && (target != txCtrl(1U)) && (target != txCtrl(2U))) { return; }
    if(pollDurationMs != 0U) { setFakeMillis(millis() + pollDurationMs); }
    if(txBehaviour == TxBehaviour::Completes) {
      registers[target] = static_cast<uint8_t>(registers[target] & static_cast<uint8_t>(~flagTxReq));
    }
  }

  static constexpr uint8_t cmdWrite = 0x02U;
  static constexpr uint8_t cmdRead = 0x03U;
  static constexpr uint8_t cmdBitModify = 0x05U;
  static constexpr uint8_t cmdReset = 0xC0U;

  uint8_t registers[registerCount] = {};
  uint8_t command = 0U;
  uint8_t address = 0U;
  uint8_t byteIndex = 0U;
  uint8_t bitModifyMask = 0U;
  TxBehaviour txBehaviour = TxBehaviour::Completes;
  uint32_t pollDurationMs = 0U;
};

extern Mcp2515Model mcp2515;

/// @brief Minimal SPIClass forwarding the byte stream to the MCP2515 model.
class SPIClass final {
public:
  void begin() {}
  void end() {}
  void beginTransaction(SPISettings /*settings*/) { mcp2515.beginMessage(); }  // NOLINT(readability-convert-member-functions-to-static) mirrors the Arduino API
  void endTransaction() {}
  uint8_t transfer(uint8_t out) { return mcp2515.transfer(out); }              // NOLINT(readability-convert-member-functions-to-static) mirrors the Arduino API
  void usingInterrupt(uint8_t /*interruptNumber*/) {}
  void notUsingInterrupt(uint8_t /*interruptNumber*/) {}
};

extern SPIClass SPI;

// Feature flags, tested with #ifdef only - valueless, as the Arduino headers declare them.
#define SPI_HAS_TRANSACTION
#define SPI_HAS_NOTUSINGINTERRUPT
