#include "canHandlerAtmega328P.hpp"
#ifdef ARDUINO_ARCH_AVR
#include "CAN.h"                                                    /// CAN controller library.
#include <Arduino.h>                                                /// Arduino libraries header.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.

volatile uint8_t CanHandlerAtmega328P::intCount = 0U;

CanHandlerAtmega328P::CanHandlerAtmega328P(DebugLedHandler& debugLed, uint8_t canCsPin, uint8_t canIntPin, uint8_t flashCsPin) :
  debugLed(debugLed),
  flash(flashCsPin, flashJedecId),
  ota(flash),
  canCallback(nullptr),
  eventTimer(0U),
  lastOtaState(OTA::OtaState::IDLE)
{
  CAN.setPins(canCsPin, -1);
  pinMode(canIntPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(canIntPin), rxInterrupt, FALLING);
}

bool CanHandlerAtmega328P::init(uint32_t canBaud) {
#if defined(NEW_CAN_ADDRESS) && defined(MASTER_CAN_ADDRESS)
  // Save new CAN IDs.
  static constexpr uint16_t newMasterCanId = static_cast<uint16_t>(MASTER_CAN_ADDRESS);
  static constexpr uint16_t newLocalCanId = static_cast<uint16_t>(NEW_CAN_ADDRESS);
  Logger::get().print(F("[CAN] New master ID: "));
  Logger::get().println(newMasterCanId);
  Logger::get().print(F("[CAN] New local ID: "));
  Logger::get().println(newLocalCanId);
  Logger::get().print(F("[CAN] Saving: "));
  const bool canIdsSavingResult = saveCanIds(newMasterCanId, newLocalCanId);
  Logger::get().println(Str::getStateStr(canIdsSavingResult));
  if(!canIdsSavingResult) { return false; }
#endif
  // Load CAN ID's.
  Logger::get().print(F("CAN IDs: "));
  if(loadCanIds()) {
    Logger::get().print(getMasterCanId());
    Logger::get().print(Str::getSpacerStr());
    Logger::get().println(getLocalCanId());
  } else {
    Logger::get().println(Str::getErrStr());
    return false;
  }
  { // Initialise SPI CAN shield.
    CAN.setClockFrequency(8E6);                     // SPI CAN controller runs from 8MHz crystal.
    CAN.setSPIFrequency(4E6);
    const bool canBeginResult = CAN.begin(canBaud) == 1;
    Logger::get().print(F("CAN: "));
    Logger::get().println(Str::getStateStr(canBeginResult));
    if(!canBeginResult) { return false; }
  }
  { // Set up the CAN filtering.
    Logger::get().print(F("Filter: "));
    const bool setFilterResult = CAN.filterExtended(
      CanHandlerBase::getCanFilteredId(), CanHandlerBase::getCanIdFilterMask()) == 1;
    Logger::get().println(Str::getStateStr(setFilterResult));
    if(!setFilterResult) { return false; }
  }
  { // Send startup info.
    const bool sendResult = CanHandlerBase::send(CanCmd::RESTART) && sendFwVersion();
    if(!sendResult) { return false; }
  }
  { // Check SPI FLASH modul.
    Logger::get().print(F("FLASH: "));
    const bool flashInitResult = flash.initialize();
    Logger::get().println(Str::getStateStr(flashInitResult));
    if(!flashInitResult) { return false; }
  }
  eventTimer = millis();
  return true;
}

bool CanHandlerAtmega328P::handleRxFrame() {
  intCount--;
  const uint8_t canDataDlc = static_cast<uint8_t>(CAN.parsePacket());
  CanFrame canFrame;
  canFrame.extId = CAN.packetId();
  if(!CAN.packetRtr()) {
    const uint8_t bytesReaded = static_cast<uint8_t>(CAN.readBytes(canFrame.data, canDataDlc));
    if(canDataDlc != bytesReaded) { return false; }
  }
  switch(static_cast<uint16_t>(canFrame.cmd)) {
    case static_cast<uint16_t>(CanCmd::PING): { CanHandlerBase::send(CanCmd::PING); } break;
    case static_cast<uint16_t>(CanCmd::RESTART): { ResetHandler::restartMCU(); } break;
    case static_cast<uint16_t>(CanCmd::FW_VERSION): { sendFwVersion(); } break;
    case static_cast<uint16_t>(CanCmd::OTA_START): {
      const uint16_t otaFlashBegin =
        static_cast<uint16_t>(canFrame.data[0]) |
        (static_cast<uint16_t>(canFrame.data[1]) << 8U);
      const uint32_t fwSize =
        static_cast<uint32_t>(canFrame.data[2]) |
        (static_cast<uint32_t>(canFrame.data[3]) << 8U) |
        (static_cast<uint32_t>(canFrame.data[4]) << 16U) |
        (static_cast<uint32_t>(canFrame.data[5]) << 24U);
      const uint16_t fwCrc =
        static_cast<uint16_t>(canFrame.data[6]) |
        (static_cast<uint16_t>(canFrame.data[7]) << 8U);
      Logger::get().print(F("OTA start: "));
      const bool otaStartResult = ota.start(otaFlashBegin, fwSize, fwCrc);
      Logger::get().println(Str::getStateStr(otaStartResult));
      if(!otaStartResult) { CanHandlerBase::send(CanCmd::OTA_START, Response::NACK); }
    } break;
    case static_cast<uint16_t>(CanCmd::OTA_SEND): {
      const uint8_t fwData[OTA::fwPieceSize] = {
        canFrame.data[0],
        canFrame.data[1],
        canFrame.data[2],
        canFrame.data[3]
      };
      const uint32_t dataAddress =
        static_cast<uint32_t>(canFrame.data[4]) |
        (static_cast<uint32_t>(canFrame.data[5]) << 8U) |
        (static_cast<uint32_t>(canFrame.data[6]) << 16U) |
        (static_cast<uint32_t>(canFrame.data[7]) << 24U);
      const bool otaStoreResult = ota.storeNextData(dataAddress, fwData);
      if(!otaStoreResult) { Logger::get().println(F("OTA storing failed!")); }
      CanHandlerBase::send(CanCmd::OTA_SEND, otaStoreResult ? Response::ACK : Response::NACK);
    } break;
    case static_cast<uint16_t>(CanCmd::OTA_END): {} break;
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
  if(lastOtaState == OTA::OtaState::START && otaState == OTA::OtaState::STORE) {
    CanHandlerBase::send(CanCmd::OTA_START, Response::ACK);
  }
  if(otaState == OTA::OtaState::VALID) {
    CanHandlerBase::send(CanCmd::OTA_END, Response::ACK);
    Logger::get().print(F("Storing: "));
    Logger::get().println(Str::getOkStr());
    if(ota.isOwnFw()) { ResetHandler::restartMCU(); }
  }
  if(otaState == OTA::OtaState::INVALID) {
    CanHandlerBase::send(CanCmd::OTA_END, Response::NACK);
    Logger::get().print(F("Storing: "));
    Logger::get().println(Str::getErrStr());
  }
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
  const bool beginPacketResult = CAN.beginExtendedPacket(extId) > 0;
  if(!beginPacketResult) { return false; }
  const bool packetWriteResult = CAN.write(data, sizeof(data)) > 0U;
  if(!packetWriteResult) { return false; }
  const bool endPacketResult = CAN.endPacket() > 0;
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