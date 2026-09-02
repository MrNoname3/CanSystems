#pragma once
// Host stand-in for the ESP32's CAN (SJA1000-compatible) peripheral.
//
// The driver reaches every register through `volatile uint32_t*` derived from a fixed base
// address, so pointing that base at a plain array is enough to run the real driver natively.
// ESP32SJA1000.cpp picks this array up under NATIVE_TEST; the ESP32 build keeps 0x3FF6B000.

#include <stdint.h>
#include <string.h>
#include <vector>
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
  static constexpr uint8_t regSff = 0x10U;    // Standard-frame receive window: header, then payload.

  static constexpr uint8_t modResetMode = 0x01U;
  static constexpr uint8_t irReceive = 0x01U;
  static constexpr uint8_t srReceiveBuffer = 0x01U;
  static constexpr uint8_t srTxBufferFree = 0x04U;
  static constexpr uint8_t srTxComplete = 0x08U;
  static constexpr uint8_t srBusOff = 0x80U;

  /// @brief A frame the driver handed to the controller, decoded from the transmit window.
  struct SentFrame {
    uint32_t id = 0U;                 // Identifier as written; an extended frame carries all 29 bits.
    uint8_t dlc = 0U;                 // Payload length.
    uint8_t data[8] = {};             // Payload.
    bool extended = false;            // Which of the two register layouts the driver used.
  };

  /// @brief Every frame the driver has requested since the last reset, in order.
  [[nodiscard]] const std::vector<SentFrame>& transmitted() const { return sentFrames; }

  /// @brief Forgets the frames recorded so far, leaving the peripheral's own state alone.
  void clearTransmitted() { sentFrames.clear(); }

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
    transmitAborts = 0U;
    sentFrames.clear();
  }

  void setTxBehaviour(TxBehaviour behaviour) { txBehaviour = behaviour; }

  /// @brief Places a standard 11-bit frame in the receive window and raises receive buffer
  /// status, the way the controller does when a frame arrives.
  /// @note The model holds one frame: the release command clears the status again.
  void queueStandardFrame(uint16_t id, const uint8_t* data, uint8_t dlc) {
    registers[regSff] = dlc & 0x0FU;
    registers[regSff + 1U] = static_cast<uint8_t>(id >> 3U);
    registers[regSff + 2U] = static_cast<uint8_t>(id << 5U);
    for(uint8_t i = 0U; i < dlc; i++) { registers[regSff + 3U + i] = data[i]; }
    registers[regSr] |= srReceiveBuffer;
  }

  /// @brief Places an extended 29-bit frame in the receive window and raises receive buffer
  /// status, the way the controller does when one arrives.
  /// @note The model holds one frame: the release command clears the status again.
  void queueExtendedFrame(uint32_t id, const uint8_t* data, uint8_t dlc) {
    registers[regSff] = static_cast<uint32_t>(0x80U | (dlc & 0x0FU));
    registers[regSff + 1U] = static_cast<uint8_t>(id >> 21U);
    registers[regSff + 2U] = static_cast<uint8_t>(id >> 13U);
    registers[regSff + 3U] = static_cast<uint8_t>(id >> 5U);
    registers[regSff + 4U] = static_cast<uint8_t>(id << 3U);
    for(uint8_t i = 0U; i < dlc; i++) { registers[regSff + 5U + i] = data[i]; }
    registers[regSr] |= srReceiveBuffer;
    registers[regIr] |= irReceive;                             // an arriving frame also raises the interrupt
  }

  /// @brief Advances the fake clock by this many milliseconds on every status poll, standing in
  /// for the time a real poll costs. 0 leaves the clock alone.
  void setPollDurationMs(uint32_t milliseconds) { pollDurationMs = milliseconds; }

  /// @brief How many times the status register was polled since the last reset.
  [[nodiscard]] uint32_t getStatusReads() const { return statusReads; }
  /// @brief How many transmission requests the driver issued since the last reset.
  [[nodiscard]] uint32_t getTransmitRequests() const { return transmitRequests; }
  /// @brief How many transmissions the driver aborted since the last reset.
  [[nodiscard]] uint32_t getTransmitAborts() const { return transmitAborts; }
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
    if((command & 0x01U) != 0U) {                                // transmission request
      ++transmitRequests;
      recordTransmit();
      // The controller holds the frame until it is on the bus or aborted.
      registers[regSr] &= ~(static_cast<uint32_t>(srTxComplete) | srTxBufferFree);
    }
    if((command & 0x02U) != 0U) {                                // abort transmission
      ++transmitAborts;
      registers[regSr] |= static_cast<uint32_t>(srTxComplete) | srTxBufferFree;
    }
    if((command & 0x04U) != 0U) {                                // release receive buffer
      registers[regSr] &= ~static_cast<uint32_t>(srReceiveBuffer);
    }
  }

  /// @brief Decodes the transmit window the driver has just filled in.
  /// @details Mirrors the layout ESP32SJA1000::endPacket() writes, which is the only writer.
  // NOLINTNEXTLINE(readability-make-member-function-const) appends to sentFrames
  void recordTransmit() {
    const uint8_t frameInfo = reg(regSff);
    SentFrame frame;
    frame.extended = (frameInfo & 0x80U) != 0U;
    frame.dlc = frameInfo & 0x0FU;
    if(frame.dlc > 8U) { frame.dlc = 8U; }
    const uint8_t dataReg = frame.extended ? static_cast<uint8_t>(regSff + 5U) : static_cast<uint8_t>(regSff + 3U);
    if(frame.extended) {
      frame.id = (static_cast<uint32_t>(reg(regSff + 1U)) << 21U) |
                 (static_cast<uint32_t>(reg(regSff + 2U)) << 13U) |
                 (static_cast<uint32_t>(reg(regSff + 3U)) << 5U) |
                 (static_cast<uint32_t>(reg(regSff + 4U)) >> 3U);
    } else {
      frame.id = (static_cast<uint32_t>(reg(regSff + 1U)) << 3U) |
                 (static_cast<uint32_t>(reg(regSff + 2U)) >> 5U);
    }
    for(uint8_t i = 0U; i < frame.dlc; i++) { frame.data[i] = reg(static_cast<uint8_t>(dataReg + i)); }
    sentFrames.push_back(frame);
  }

  void onStatusRead() {
    ++statusReads;
    if(pollDurationMs != 0U) { setFakeMillis(millis() + pollDurationMs); }
    // Nothing has been sent yet, so the transmit buffer is simply free.
    if(transmitRequests == 0U) { return; }
    switch(txBehaviour) {
      case TxBehaviour::Completes: {
        registers[regSr] |= static_cast<uint32_t>(srTxComplete) | srTxBufferFree;
      } break;
      case TxBehaviour::NeverEnds: {
        // Nothing acknowledges, so the controller keeps retransmitting and the buffer stays busy.
        registers[regSr] &= ~(static_cast<uint32_t>(srTxComplete) | srTxBufferFree);
      } break;
      case TxBehaviour::BusOff: {
        // The transmit error counter runs over and the controller drops into bus-off, which on
        // this part means it sets reset mode and stays there until software clears it.
        registers[regSr] &= ~(static_cast<uint32_t>(srTxComplete) | srTxBufferFree);
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
  uint32_t transmitAborts = 0U;
  std::vector<SentFrame> sentFrames;
};

extern Esp32CanModel esp32Can;

/// @brief Base address the driver builds its register pointers from on the host.
uint32_t* esp32CanRegisterFile();

/// @brief Hook the driver calls around every register access so the model can react.
void esp32CanOnAccess(uint8_t address, bool isWrite);
