#include "canHandler.hpp"
#include <Arduino.h>                                                /// Arduino libraries header.
#include <CAN.h>                                                    /// CAN controller library.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include <avr/boot.h>                                               /// Reading fuses.

volatile uint8_t CanHandler::intCount = 0U;
static constexpr uint16_t fwVersion = static_cast<uint16_t>(GIT_COMMIT_COUNT);
static constexpr uint32_t gitHash = static_cast<uint32_t>(GIT_COMMIT_HASH);
static constexpr uint8_t gitDirty = static_cast<uint8_t>(GIT_DIRTY);

CanHandler::CanHandler(HardwareSerial& serial, uint8_t canCsPin, uint8_t canIntPin, uint8_t ledPin, uint8_t flashCsPin) :
  serialPort(serial),
  localCanId(0U),
  eepromHandler(&localCanId),
  ledPin(ledPin),
  flash(flashCsPin, flashJedecId),
  ota(flash),
  canCallback(nullptr),
  eventTimer(0U),
  lastOtaState(OTA::OtaState::IDLE)
{
  CAN.setPins(canCsPin, -1);
  pinMode(canIntPin, INPUT);
  pinMode(ledPin, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(canIntPin), rxInterrupt, FALLING);
}

bool CanHandler::init(uint32_t canBaud) {
  {
    constexpr uint32_t cppVersion = static_cast<uint32_t>(__cplusplus);
    const char* SPACER = "|";
    serialPort.print(F("CPP: "));
    serialPort.println(cppVersion);
    serialPort.print(F("FW: "));
    serialPort.println(fwVersion);
    serialPort.print(F("GIT: "));
    serialPort.println(gitHash, HEX);
    serialPort.print(F("Dirty: "));
    serialPort.println(gitDirty);
    serialPort.print(F("Fuses: "));
    serialPort.print(boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS), HEX);
    serialPort.print(SPACER);
    serialPort.print(boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS), HEX);
    serialPort.print(SPACER);
    serialPort.print(boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS), HEX);
    serialPort.print(SPACER);
    serialPort.println(boot_lock_fuse_bits_get(GET_LOCK_BITS), HEX);
  }
  {
#ifdef NEW_CAN_ADDRESS
    localCanId = newCanAddress;
    eepromHandler.save();
#endif
    serialPort.print(F("Address: "));
    const bool eepromDataValid = eepromHandler.load();
    eepromDataValid ? serialPort.println(localCanId) : serialPort.println(ERR_STATE);
    if(!eepromDataValid) { return false; }
  }
  {
    serialPort.print(F("CAN: "));
    CAN.setClockFrequency(8E6);                     // SPI CAN controller runs from 8MHz crystal.
    CAN.setSPIFrequency(4E6);
    const bool canBeginResult = CAN.begin(canBaud) == 1;
    canBeginResult ? serialPort.println(OK_STATE) : serialPort.println(ERR_STATE);
    if(!canBeginResult) { return false; }
  }
  { // Calculate the mask to ignore the upper bits of the extended CAN ID and only consider the lower 10 bits.
    const uint16_t deviceAddress = localCanId;
    const uint32_t mask = 0x3FFUL;                  // Mask for lower 10 bits (0b1111111111).
    const uint32_t id = deviceAddress & mask;       // Calculate the ID using the device's local address.
    serialPort.print(F("Filter: "));
    const bool setFilterResult = CAN.filterExtended(id, mask) == 1;
    setFilterResult ? serialPort.println(OK_STATE) : serialPort.println(ERR_STATE);
    if(!setFilterResult) { return false; }
  }
  {
    const bool sendResult = send(CanCmd::RESTART) && sendFwVersion();
    if(!sendResult) { return false; }
  }
  {
    serialPort.print(F("FLASH: "));
    const bool flashInitResult = flash.initialize();
    flashInitResult ? serialPort.println(OK_STATE) : serialPort.println(ERR_STATE);
    if(!flashInitResult) { return false; }
  }
  eventTimer = millis();
  return true;
}

void CanHandler::run() {
  const bool loopResult = loopSimple();
  if(!loopResult) {
    serialPort.println(F("Loop ERROR!"));
    ResetHandler::restartMCU();
  }
}

bool CanHandler::loopSimple() {
  const uint32_t actualTime = millis();
  if(intCount > 0U) {
    intCount--;
    eventTimer = actualTime;
    ledOff();
    const uint8_t canDataDlc = static_cast<uint8_t>(CAN.parsePacket());
    CanFrame canFrame;
    canFrame.extId = CAN.packetId();
    if(!CAN.packetRtr()) {
      const uint8_t bytesReaded = static_cast<uint8_t>(CAN.readBytes(canFrame.data, canDataDlc));
      if(canDataDlc != bytesReaded) { return false; }
    }
    if(CAN.packetExtended()) {
      switch(static_cast<uint16_t>(canFrame.cmd)) {
        case static_cast<uint16_t>(CanCmd::PING): { send(CanCmd::PING); } break;
        case static_cast<uint16_t>(CanCmd::RESTART): { return false; } break;  // Trigger MCU restart.
        case static_cast<uint16_t>(CanCmd::FW_VERSION): { sendFwVersion(); } break;
        case static_cast<uint16_t>(CanCmd::OTA_START): {
          const uint16_t otaFlashBegin =
            static_cast<uint16_t>(canFrame.data[0]) << 0U |
            static_cast<uint16_t>(canFrame.data[1]) << 8U;
          const uint32_t fwSize =
            static_cast<uint32_t>(canFrame.data[2]) << 0U |
            static_cast<uint32_t>(canFrame.data[3]) << 8U |
            static_cast<uint32_t>(canFrame.data[4]) << 16U |
            static_cast<uint32_t>(canFrame.data[5]) << 24U;
          const uint16_t fwCrc =
            static_cast<uint16_t>(canFrame.data[6]) << 0U |
            static_cast<uint16_t>(canFrame.data[7]) << 8U;
          serialPort.print(F("OTA start: "));
          const bool otaStartResult = ota.start(otaFlashBegin, fwSize, fwCrc);
          otaStartResult ? serialPort.println(OK_STATE) : serialPort.println(ERR_STATE);
          if(!otaStartResult) { send(CanCmd::OTA_START, Response::NACK); }
        } break;
        case static_cast<uint16_t>(CanCmd::OTA_SEND): {
          const uint8_t fwData[ota.fwPieceSize] = {
            canFrame.data[0],
            canFrame.data[1],
            canFrame.data[2],
            canFrame.data[3]
          };
          const uint32_t dataAddress =
            static_cast<uint32_t>(canFrame.data[4]) << 0U |
            static_cast<uint32_t>(canFrame.data[5]) << 8U |
            static_cast<uint32_t>(canFrame.data[6]) << 16U |
            static_cast<uint32_t>(canFrame.data[7]) << 24U;
          const bool otaStoreResult = ota.storeNextData(dataAddress, fwData);
          if(!otaStoreResult) { serialPort.println(F("OTA storing failed!")); }
          send(CanCmd::OTA_SEND, otaStoreResult ? Response::ACK : Response::NACK);
        } break;
        case static_cast<uint16_t>(CanCmd::OTA_END): {} break;
        default: {
          if(canCallback != nullptr) {
            canCallback(static_cast<uint16_t>(canFrame.cmd), canFrame.data);
          }
        } break;
      }
    }
  }
  const OTA::OtaState otaState = ota.run();
  if(lastOtaState == OTA::OtaState::START && otaState == OTA::OtaState::STORE) {
    send(CanCmd::OTA_START, Response::ACK);
  }
  if(otaState == OTA::OtaState::VALID) {
    send(CanCmd::OTA_END, Response::ACK);
    serialPort.print(F("Storing: "));
    serialPort.println(OK_STATE);
    if(ota.isOwnFw()) { return false; } // Trigger MCU restart.
  }
  if(otaState == OTA::OtaState::INVALID) {
    send(CanCmd::OTA_END, Response::NACK);
    serialPort.print(F("Storing: "));
    serialPort.println(ERR_STATE);
  }
  lastOtaState = otaState;
  if(Time::hasElapsed(actualTime, eventTimer, pingTime)) {
    ledOn();
  }
  return true;
}

bool CanHandler::send(uint16_t command, const uint8_t (&data)[8]) const {
  const uint32_t extId =
    ((static_cast<uint32_t>(masterCanId) & 0x3FF) << 0U) |
    ((static_cast<uint32_t>(command) & 0x1FF) << 10U) |
    ((static_cast<uint32_t>(localCanId) & 0x3FF) << 19U);
  const bool beginPacketResult = CAN.beginExtendedPacket(extId) > 0;
  if(!beginPacketResult) { return false; }
  const bool packetWriteResult = CAN.write(data, sizeof(data)) > 0U;
  if(!packetWriteResult) { return false; }
  const bool endPacketResult = CAN.endPacket() > 0;
  if(!endPacketResult) { return false; }
  return true;
}

bool CanHandler::send(CanCmd command, const uint8_t (&data)[8]) const {
  return send(static_cast<uint16_t>(command), data);
}

bool CanHandler::send(uint16_t command) const {
  const uint8_t data[8] = {0U};
  return send(command, data);
}

bool CanHandler::send(CanCmd command) const {
  return send(static_cast<uint16_t>(command));
}

bool CanHandler::send(CanCmd command, Response response) const {
  const uint8_t data[8] = {static_cast<uint8_t>(response), 0U, 0U, 0U, 0U, 0U, 0U, 0U};
  return send(command, data);
}

bool CanHandler::sendFwVersion() const {
  static constexpr uint8_t versionInfo[8] = {
    static_cast<uint8_t>((fwVersion >> 0U) & 0xFF),
    static_cast<uint8_t>((fwVersion >> 8U) & 0xFF),
    static_cast<uint8_t>((gitHash >> 0U) & 0xFF),
    static_cast<uint8_t>((gitHash >> 8U) & 0xFF),
    static_cast<uint8_t>((gitHash >> 16U) & 0xFF),
    static_cast<uint8_t>((gitHash >> 24U) & 0xFF),
    gitDirty,
    0U
  };
  return send(CanCmd::FW_VERSION, versionInfo);
}