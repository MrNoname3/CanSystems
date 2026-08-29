#pragma once
// Host stand-in for the ESP32's CAN (SJA1000-compatible) peripheral.
//
// The driver reaches every register through `volatile uint32_t*` derived from a fixed base
// address, so pointing that base at a plain array is enough to run the real driver natively.
// ESP32SJA1000.cpp picks this array up under NATIVE_TEST; the ESP32 build keeps 0x3FF6B000.

#include <stdint.h>
#include <string.h>
#include "Arduino.h"

/// @brief Register-level SJA1000 stand-in: the register file plus the behaviour tests drive.
class Esp32CanModel final {
public:
  static constexpr uint8_t registerCount = 32U;

  // Register addresses the model reacts to (SJA1000 PeliCAN layout).
  static constexpr uint8_t regMod = 0x00U;    // Mode: bit 0 = reset mode.
  static constexpr uint8_t regCmr = 0x01U;    // Command: bit 0 = transmission request, bit 1 = abort.
  static constexpr uint8_t regSr = 0x02U;     // Status: bit 2 = TX buffer free, bit 3 = TX complete.
  static constexpr uint8_t regIr = 0x03U;     // Interrupt.
  static constexpr uint8_t regEcc = 0x0CU;    // Error code capture.
  static constexpr uint8_t regTxErr = 0x0FU;  // Transmit error counter.

  static constexpr uint8_t modResetMode = 0x01U;
  static constexpr uint8_t srTxBufferFree = 0x04U;
  static constexpr uint8_t srTxComplete = 0x08U;
  static constexpr uint8_t srBusOff = 0x80U;

  /// @brief How the peripheral answers a transmission request.
  enum class TxBehaviour : uint8_t {
    Completes,   // The frame goes out: TX complete is reported straight away.
    NeverEnds,   // Nothing on the bus acknowledges: TX complete never appears.
    BusOff       // The error counter runs over: the controller drops into bus-off.
  };

  /// @brief Clears the register file and puts the peripheral in its powered-up state.
  void reset() {
    memset(registers, 0, sizeof(registers));
    registers[regSr] = static_cast<uint32_t>(srTxBufferFree) | srTxComplete;
    txBehaviour = TxBehaviour::Completes;
    pollDurationMs = 0U;
    statusReads = 0U;
    transmitRequests = 0U;
  }

  void setTxBehaviour(TxBehaviour behaviour) { txBehaviour = behaviour; }

  /// @brief Advances the fake clock by this many milliseconds on every status poll, standing in
  /// for the time a real poll costs. 0 leaves the clock alone.
  void setPollDurationMs(uint32_t milliseconds) { pollDurationMs = milliseconds; }

  /// @brief How many times the status register was polled since the last reset.
  [[nodiscard]] uint32_t getStatusReads() const { return statusReads; }
  /// @brief How many transmission requests the driver issued since the last reset.
  [[nodiscard]] uint32_t getTransmitRequests() const { return transmitRequests; }
  [[nodiscard]] bool isInResetMode() const { return (registers[regMod] & modResetMode) != 0U; }

  [[nodiscard]] uint32_t* file() { return registers; }
  [[nodiscard]] uint8_t reg(uint8_t address) const { return static_cast<uint8_t>(registers[address & 0x1FU]); }
  void setReg(uint8_t address, uint8_t value) { registers[address & 0x1FU] = value; }

  /// @brief Applies the modelled peripheral behaviour; called on every register access.
  /// @param address Register being touched.
  /// @param isWrite `true` for a write, `false` for a read.
  void onAccess(uint8_t address, bool isWrite) {
    if(isWrite && (address == regCmr)) { onCommand(); }
    if(!isWrite && (address == regSr)) { onStatusRead(); }
  }

private:
  void onCommand() {
    const uint8_t command = static_cast<uint8_t>(registers[regCmr]);
    if((command & 0x01U) != 0U) { ++transmitRequests; }         // transmission request
    if((command & 0x02U) != 0U) {                                // abort transmission
      registers[regSr] |= static_cast<uint32_t>(srTxComplete) | srTxBufferFree;
    }
  }

  void onStatusRead() {
    ++statusReads;
    if(pollDurationMs != 0U) { setFakeMillis(millis() + pollDurationMs); }
    switch(txBehaviour) {
      case TxBehaviour::Completes: {
        registers[regSr] |= static_cast<uint32_t>(srTxComplete) | srTxBufferFree;
      } break;
      case TxBehaviour::NeverEnds: {
        registers[regSr] &= ~static_cast<uint32_t>(srTxComplete);
      } break;
      case TxBehaviour::BusOff: {
        // The transmit error counter runs over and the controller drops into bus-off, which on
        // this part means it sets reset mode and stays there until software clears it.
        registers[regSr] &= ~static_cast<uint32_t>(srTxComplete);
        registers[regSr] |= srBusOff;
        registers[regTxErr] = 255U;
        registers[regMod] |= modResetMode;
      } break;
    }
  }

  uint32_t registers[registerCount] = {};
  TxBehaviour txBehaviour = TxBehaviour::Completes;
  uint32_t pollDurationMs = 0U;
  uint32_t statusReads = 0U;
  uint32_t transmitRequests = 0U;
};

extern Esp32CanModel esp32Can;

/// @brief Base address the driver builds its register pointers from on the host.
uint32_t* esp32CanRegisterFile();

/// @brief Hook the driver calls around every register access so the model can react.
void esp32CanOnAccess(uint8_t address, bool isWrite);
