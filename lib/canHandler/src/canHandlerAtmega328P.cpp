#include "canHandlerAtmega328P.hpp"
#ifdef ARDUINO_ARCH_AVR
#include "CAN.h"                                                    /// CAN controller library.
#include <Arduino.h>                                                /// Arduino libraries header.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include "otaCanFrame.hpp"                                          /// Shared OTA-over-CAN frame layout (pack/unpack).
#include "otaCanResponse.hpp"                                       /// Host-testable OTA state -> CAN response decision.
#include <util/atomic.h>                                            /// ATOMIC_BLOCK for ISR-shared variable access.

// The OTA_SEND piece carried on the wire must match the size OTA storage consumes per chunk,
// otherwise unpackSend() would hand storeNextData() a wrongly-sized array.
static_assert(OtaCanFrame::dataPieceSize == OTA::fwPieceSize, "OTA CAN piece size must equal OTA::fwPieceSize");

namespace {
  constexpr const char PROGMEM storingStr[] = "Storing: ";
} // namespace

volatile uint8_t CanHandlerAtmega328P::intCount = 0U;

CanHandlerAtmega328P::CanHandlerAtmega328P(DebugLedHandler& debugLed, uint8_t canCsPin, uint8_t canIntPin, uint8_t flashCsPin) :
  debugLed(debugLed),
  flash(flashCsPin, flashJedecId),
  ota(flash),
  canCallback(nullptr),
  eventTimer(0U),
  lastOtaState(OTA::OtaState::IDLE) {
  CAN.setPins(canCsPin, 0xFFU);
  pinMode(canIntPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(canIntPin), rxInterrupt, FALLING);
}

bool CanHandlerAtmega328P::init(uint32_t canBaud) {
#if defined(NEW_CAN_ADDRESS) && defined(MASTER_CAN_ADDRESS)
  // Save new CAN IDs.
  static constexpr uint16_t newMasterCanId = static_cast<uint16_t>(MASTER_CAN_ADDRESS);
  static constexpr uint16_t newLocalCanId = static_cast<uint16_t>(NEW_CAN_ADDRESS);
  Logger::get()->print(F("[CAN] New master ID: "));
  Logger::get()->println(newMasterCanId);
  Logger::get()->print(F("[CAN] New local ID: "));
  Logger::get()->println(newLocalCanId);
  Logger::get()->print(F("[CAN] Saving: "));
  const bool canIdsSavingResult = saveCanIds(newMasterCanId, newLocalCanId);
  Logger::get()->println(Str::getStateStr(canIdsSavingResult));
  if(!canIdsSavingResult) { return false; }
#endif
  // Load CAN ID's.
  Logger::get()->print(F("CAN IDs: "));
  if(loadCanIds()) {
    Logger::get()->print(getMasterCanId());
    Logger::get()->print(Str::getSpacerStr());
    Logger::get()->println(getLocalCanId());
  } else {
    Logger::get()->println(Str::getErrStr());
    return false;
  }
  { // Initialise SPI CAN shield.
    CAN.setClockFrequency(8E6);                     // SPI CAN controller runs from 8MHz crystal.
    CAN.setSPIFrequency(4E6);
    const bool canBeginResult = CAN.begin(canBaud) == 1U;
    Logger::get()->print(F("CAN: "));
    Logger::get()->println(Str::getStateStr(canBeginResult));
    if(!canBeginResult) { return false; }
  }
  { // Set up the CAN filtering.
    Logger::get()->print(F("Filter: "));
    const bool setFilterResult = CAN.filterExtended(CanHandlerBase::getCanFilteredId(), CanHandlerBase::getCanIdFilterMask()) == 1U;
    Logger::get()->println(Str::getStateStr(setFilterResult));
    if(!setFilterResult) { return false; }
  }
  { // Send startup info.
    const bool sendResult = CanHandlerBase::send(CanCmd::RESTART) && sendFwVersion();
    if(!sendResult) { return false; }
  }
  { // Check SPI FLASH modul.
    Logger::get()->print(F("FLASH: "));
    const bool flashInitResult = flash.initialize();
    Logger::get()->println(Str::getStateStr(flashInitResult));
    if(!flashInitResult) { return false; }
  }
  eventTimer = millis();
  return true;
}

bool CanHandlerAtmega328P::handleRxFrame() {
  // The decrement is a non-atomic read-modify-write; an rxInterrupt between the load and the
  // store would lose its increment (and with edge-triggered INT, the pending frame with it).
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { intCount--; }
  const uint8_t canDataDlc = CAN.parsePacket();
  CanFrame canFrame;
  canFrame.extId = CAN.packetId();
  if(!CAN.packetRtr()) {
    const uint8_t bytesReaded = static_cast<uint8_t>(CAN.readBytes(canFrame.data, canDataDlc));
    if(canDataDlc != bytesReaded) { return false; }
  }
  switch(static_cast<uint16_t>(canFrame.cmd)) {
    case static_cast<uint16_t>(CanCmd::PING): {
      CanHandlerBase::send(CanCmd::PING);
    } break;
    case static_cast<uint16_t>(CanCmd::RESTART): {
      ResetHandler::restartMCU();
    } break;
    case static_cast<uint16_t>(CanCmd::FW_VERSION): {
      sendFwVersion();
    } break;
    case static_cast<uint16_t>(CanCmd::OTA_START): {
      const OtaCanFrame::StartFrame startFrame = OtaCanFrame::unpackStart(canFrame.data);
      Logger::get()->print(F("OTA start: "));
      const bool otaStartResult = ota.start(startFrame.storageNumber, startFrame.fwSize, startFrame.fwCrc);
      Logger::get()->println(Str::getStateStr(otaStartResult));
      if(!otaStartResult) { CanHandlerBase::send(CanCmd::OTA_START, Response::NACK); }
    } break;
    case static_cast<uint16_t>(CanCmd::OTA_SEND): {
      const OtaCanFrame::SendFrame sendFrame = OtaCanFrame::unpackSend(canFrame.data);
      const bool otaStoreResult = ota.storeNextData(sendFrame.dataAddress, sendFrame.data);
      if(!otaStoreResult) { Logger::get()->println(F("OTA storing failed!")); }
      CanHandlerBase::send(CanCmd::OTA_SEND, otaStoreResult ? Response::ACK : Response::NACK);
    } break;
    case static_cast<uint16_t>(CanCmd::OTA_END): {
    } break;
    default: {
      if(canCallback != nullptr) {
        canCallback(static_cast<uint16_t>(canFrame.cmd), canFrame.data);
      }
    } break;
  }
  return true;
}

bool CanHandlerAtmega328P::run() {
  const uint32_t actualTime = millis();
  if(intCount > 0U) {
    eventTimer = actualTime;
    DebugLedHandler::ledOff();
    if(!handleRxFrame()) { return false; }
  }
  const OTA::OtaState otaState = ota.run();
  const OtaCanResponse::Decision otaDecision = OtaCanResponse::decide(lastOtaState, otaState, ota.isOwnFw());
  switch(otaDecision.action) {
    case OtaCanResponse::Action::ACK_START: {
      CanHandlerBase::send(CanCmd::OTA_START, Response::ACK);
    } break;
    case OtaCanResponse::Action::ACK_END: {
      CanHandlerBase::send(CanCmd::OTA_END, Response::ACK);
      Logger::get()->print(reinterpret_cast<const __FlashStringHelper*>(storingStr));
      Logger::get()->println(Str::getOkStr());
    } break;
    case OtaCanResponse::Action::NACK_END: {
      CanHandlerBase::send(CanCmd::OTA_END, Response::NACK);
      Logger::get()->print(reinterpret_cast<const __FlashStringHelper*>(storingStr));
      Logger::get()->println(Str::getErrStr());
    } break;
    case OtaCanResponse::Action::NONE: {
    } break;
  }
  if(otaDecision.reboot) { ResetHandler::restartMCU(); }
  lastOtaState = otaState;
  if(Time::hasElapsed(actualTime, eventTimer, pingTime)) {
    DebugLedHandler::ledOn();
  }
  return true;
}

bool CanHandlerAtmega328P::send(uint16_t command, const uint8_t (&data)[8]) const {
  const uint32_t extId =
      (static_cast<uint32_t>(getMasterCanId()) & 0x3FF) |
      ((static_cast<uint32_t>(command) & 0x1FF) << 10U) |
      ((static_cast<uint32_t>(getLocalCanId()) & 0x3FF) << 19U);
  const bool beginPacketResult = CAN.beginExtendedPacket(extId, sizeof(data)) != 0U;
  if(!beginPacketResult) { return false; }
  const bool packetWriteResult = CAN.write(data, sizeof(data)) != 0U;
  if(!packetWriteResult) { return false; }
  const bool endPacketResult = CAN.endPacket() != 0U;
  return endPacketResult;
}

bool CanHandlerAtmega328P::sendFwVersion() const { // NOLINT(readability-convert-member-functions-to-static)
  static constexpr uint8_t versionInfo[8] = {
    static_cast<uint8_t>(Build::getFwVersion() & 0xFF),
    static_cast<uint8_t>((Build::getFwVersion() >> 8U) & 0xFF),
    static_cast<uint8_t>(Build::getGitHash() & 0xFF),
    static_cast<uint8_t>((Build::getGitHash() >> 8U) & 0xFF),
    static_cast<uint8_t>((Build::getGitHash() >> 16U) & 0xFF),
    static_cast<uint8_t>((Build::getGitHash() >> 24U) & 0xFF),
    Build::getGitDirty(),
    0U
  };
  return CanHandlerBase::send(CanCmd::FW_VERSION, versionInfo);
}
#endif // ARDUINO_ARCH_AVR