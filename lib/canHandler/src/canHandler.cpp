#if defined(__AVR_ATmega328P__)
#include "canHandler.hpp"
#include <Arduino.h>                                                /// Arduino libraries header.
#include <CAN.h>                                                    /// CAN controller library.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include <avr/boot.h>                                               /// Reading fuses.

volatile uint8_t CanHandler::intCount = 0U;

CanHandler::CanHandler(HardwareSerial& serial, DebugLedHandler& debugLed, uint8_t canCsPin, uint8_t canIntPin, uint8_t flashCsPin) :
  serialPort(serial),
  debugLed(debugLed),
  localCanId(0U),
  eepromHandler(&localCanId),
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

bool CanHandler::init(uint32_t canBaud) {
  {
    serialPort.print(F("CPP: "));
    serialPort.println(Build::getCppVersion());
    serialPort.print(F("FW: "));
    serialPort.println(Build::getFwVersion());
    serialPort.print(F("GIT: "));
    serialPort.println(Build::getGitHash(), HEX);
    serialPort.print(F("Dirty: "));
    serialPort.println(Build::getGitDirty());
    serialPort.print(F("Fuses: "));
    serialPort.print(boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS), HEX);
    serialPort.print(Str::getSpacerStr());
    serialPort.print(boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS), HEX);
    serialPort.print(Str::getSpacerStr());
    serialPort.print(boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS), HEX);
    serialPort.print(Str::getSpacerStr());
    serialPort.println(boot_lock_fuse_bits_get(GET_LOCK_BITS), HEX);
  }
  {
#ifdef NEW_CAN_ADDRESS
    localCanId = newCanAddress;
    eepromHandler.save();
#endif
    serialPort.print(F("Address: "));
    const bool eepromDataValid = eepromHandler.load();
    eepromDataValid ? serialPort.println(localCanId) : serialPort.println(Str::getErrStr());
    if(!eepromDataValid) { return false; }
  }
  {
    serialPort.print(F("CAN: "));
    CAN.setClockFrequency(8E6);                     // SPI CAN controller runs from 8MHz crystal.
    CAN.setSPIFrequency(4E6);
    const bool canBeginResult = CAN.begin(canBaud) == 1;
    serialPort.println(Str::getStateStr(canBeginResult));
    if(!canBeginResult) { return false; }
  }
  { // Calculate the mask to ignore the upper bits of the extended CAN ID and only consider the lower 10 bits.
    const uint16_t deviceAddress = localCanId;
    const uint32_t mask = 0x3FFUL;                  // Mask for lower 10 bits (0b1111111111).
    const uint32_t id = deviceAddress & mask;       // Calculate the ID using the device's local address.
    serialPort.print(F("Filter: "));
    const bool setFilterResult = CAN.filterExtended(id, mask) == 1;
    serialPort.println(Str::getStateStr(setFilterResult));
    if(!setFilterResult) { return false; }
  }
  {
    const bool sendResult = send(CanCmd::RESTART) && sendFwVersion();
    if(!sendResult) { return false; }
  }
  {
    serialPort.print(F("FLASH: "));
    const bool flashInitResult = flash.initialize();
    serialPort.println(Str::getStateStr(flashInitResult));
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
    debugLed.ledOff();
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
        case static_cast<uint16_t>(CanCmd::RESTART): { ResetHandler::restartMCU(); } break;
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
          serialPort.println(Str::getStateStr(otaStartResult));
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
    serialPort.println(Str::getOkStr());
    if(ota.isOwnFw()) { ResetHandler::restartMCU(); }
  }
  if(otaState == OTA::OtaState::INVALID) {
    send(CanCmd::OTA_END, Response::NACK);
    serialPort.print(F("Storing: "));
    serialPort.println(Str::getErrStr());
  }
  lastOtaState = otaState;
  if(Time::hasElapsed(actualTime, eventTimer, pingTime)) {
    debugLed.ledOn();
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
    static_cast<uint8_t>((Build::getFwVersion() >> 0U) & 0xFF),
    static_cast<uint8_t>((Build::getFwVersion() >> 8U) & 0xFF),
    static_cast<uint8_t>((Build::getGitHash() >> 0U) & 0xFF),
    static_cast<uint8_t>((Build::getGitHash() >> 8U) & 0xFF),
    static_cast<uint8_t>((Build::getGitHash() >> 16U) & 0xFF),
    static_cast<uint8_t>((Build::getGitHash() >> 24U) & 0xFF),
    Build::getGitDirty(),
    0U
  };
  return send(CanCmd::FW_VERSION, versionInfo);
}

#elif defined(ESP32)

#include "canHandler.hpp"
#include <CAN.h>                              /// CAN controller library.
#include <ArduinoJson.h>                      /// Handle JSON files.
#include <cstdlib>

QueueHandle_t CanHandler::canRxQueue = nullptr;
const char CanHandler::OK_STATE[] PROGMEM                 = " [OK]";                    // OK status.
const char CanHandler::ERR_STATE[] PROGMEM                = " [ERR]";                   // Error status.

CanHandler::CanHandler(HardwareSerial& serial) : serialPort(serial) {}

bool CanHandler::begin(uint32_t canBaud) {
  serialPort.printf_P(PSTR("[CAN] Hardware init started...\r\n"));
  const bool canBeginResult = CAN.begin(canBaud) == 1;
  serialPort.printf_P(PSTR("[CAN] Init:%s\r\n"), (canBeginResult ? OK_STATE : ERR_STATE));
  if(!canBeginResult) { return false; }
  CAN.onReceive(rxInterrupt);
  { // Calculate the mask to ignore the upper bits of the extended CAN ID and only consider the lower 10 bits.
    const uint16_t deviceAddress = localCanId;
    const uint32_t mask = 0x3FFU;                   // Mask for lower 10 bits (0b1111111111).
    const uint32_t id = deviceAddress & mask;       // Calculate the ID using the device's local address.
    const bool setFilterResult = CAN.filterExtended(id, mask) == 1;
    serialPort.printf_P(PSTR("[CAN] Filter:%s\r\n"), (setFilterResult ? OK_STATE : ERR_STATE));
    if(!setFilterResult) { return false; }
  }
  canRxQueue = xQueueCreate(canRxQueueSize, sizeof(CanFrame));  // Create FIFO queue for RX CAN packets.
  const bool rxQueueResult = canRxQueue != nullptr;             // Check queue creation.
  canTxQueue = xQueueCreate(canTxQueueSize, sizeof(CanFrame));  // Create FIFO queue for TX CAN packets.
  const bool txQueueResult = canTxQueue != nullptr;
  serialPort.printf_P(PSTR("[CAN] Creating queues:%s\r\n"), (rxQueueResult && txQueueResult ? OK_STATE : ERR_STATE));
  if(!rxQueueResult || !txQueueResult) { return false; }
  serialPort.printf_P(PSTR("[CAN] Init registered objects:\r\n"));
  for(std::size_t i = 0; i < canDevices.size(); ++i) {
    const auto& currentObject = canDevices[i];
    if(currentObject != nullptr) {
      const bool beginResult = currentObject->beginPriv();
      serialPort.printf_P(PSTR("  %zu. %u ->%s\r\n"), i, currentObject->getCanId(), beginResult ? OK_STATE : ERR_STATE);
    }
    else {
      serialPort.printf_P(PSTR("  %zu. No object here!\r\n"), i);
    }
  }
  return true;
}

void CanHandler::rxInterrupt(int packetsNum) {
  if(packetsNum <= 0) { return; }
  if(!CAN.packetExtended()) { return; }
  CanFrame rxCanData;
  rxCanData.extId = CAN.packetId();
  if(!CAN.packetRtr()) {
    const uint8_t canDataDlc = static_cast<uint8_t>(CAN.packetDlc());
    const uint8_t bytesReaded = static_cast<uint8_t>(CAN.readBytes(rxCanData.data, canDataDlc));
    if(canDataDlc != bytesReaded) { return; }
  }
  if(xQueueSend(canRxQueue, &rxCanData, 50U) != pdTRUE) { return; }
}

bool CanHandler::send(const CanFrame& frameOut) {
  const bool qSendResult = xQueueSend(canTxQueue, &frameOut, 50U) == pdTRUE;
  return qSendResult;
}

bool CanHandler::loop() {
  { // Handle received CAN frame.
    CanFrame frameIn;
    const bool qReceiveResult = xQueueReceive(canRxQueue, &frameIn, 0U) == pdTRUE;
    if(qReceiveResult) {
      const uint32_t nodeCanId = frameIn.from;
      for(const auto &currentObject : canDevices) {
        if(currentObject == nullptr) { return false; }
        if(currentObject->getCanId() == nodeCanId) {
          currentObject->canFrameReceivedPriv(frameIn);
        }
      }
    }
  }
  // Handle the looping of subclasses.
  for(const auto &currentObject : canDevices) {
    if(currentObject != nullptr) {
      currentObject->loopPriv();
    }
  }
  { // Handle CAN frame sending.
    CanFrame frameOut;
    const bool qReceiveResult = xQueueReceive(canTxQueue, &frameOut, 0U) == pdTRUE;
    if(qReceiveResult) {
      const bool beginPacketResult = CAN.beginExtendedPacket(frameOut.extId) > 0;
      if(!beginPacketResult) { return false; }
      const bool packetWriteResult = CAN.write(frameOut.data, sizeof(frameOut.data)) > 0;
      if(!packetWriteResult) { return false; }
      const bool endPacketResult = CAN.endPacket() > 0;
      if(!endPacketResult) { return false; }
    }
  }
  return true;
}

bool CanHandler::registerCallback(CanHandler::CanComBase* obj) {
  if(!obj) { return false; }
  canDevices.push_back(obj);
  return true;
}

//////////////////// -- SoftwareTimer class-- ////////////////////

CanHandler::SoftwareTimer::SoftwareTimer(uint32_t time) : time_(time), start_time_(0) {}

bool CanHandler::SoftwareTimer::isExpired() { return (millis() - start_time_ >= time_); }

void CanHandler::SoftwareTimer::reload() { start_time_ = millis(); }

//////////////////// -- CanComBase class-- ////////////////////

const char CanHandler::CanComBase::STATUS_ONLINE[] PROGMEM              = "ONLINE";
const char CanHandler::CanComBase::STATUS_OFFLINE[] PROGMEM             = "OFFLINE";
const char CanHandler::CanComBase::STATUS_RESTARTED[] PROGMEM           = "RESTARTED";
const char CanHandler::CanComBase::STATUS_FRAME[] PROGMEM = {
  "{"
    "\"Status\":\"%s\""
  "}"
};
const char CanHandler::CanComBase::BUTTON_FRAME[] PROGMEM = {
  "{"
    "\"Button\":%hu"
  "}"
};
const char CanHandler::CanComBase::FW_VERSION_FRAME[] PROGMEM = {
  "{"
    "\"Firmware\":%hu,"
    "\"GitHash\":\"%x\""
  "}"
};
const char CanHandler::CanComBase::OTA_FRAME[] PROGMEM = {
  "{"
    "\"OTA\":\"%s\""
  "}"
};

CanHandler::CanComBase::CanComBase(CanHandler& canHandler, uint32_t canId, Connectivity& connectivity, const char* classID) :
  MqttComBase(connectivity, classID),
  canHandler(canHandler),
  nodeCanId(canId),
  pingTimer(pingTime),
  alertTimer(alertTime),
  nodeAlive_(true),
  receivedFile(),
  frameNumber(0),
  storageNumber(0),
  transferState(TransferState::IDLE),
  otaTimeoutTimer(otaTimeoutTime)
{
  canHandler.registerCallback(this);
}

bool CanHandler::CanComBase::beginPriv() {
  sendCanCmd(CanCmd::PING);
  pingTimer.reload();
  alertTimer.reload();
  return init();
}

bool CanHandler::CanComBase::loopPriv() {
  if(pingTimer.isExpired()) {
    pingTimer.reload();
    sendCanCmd(CanCmd::PING);
  }
  const bool nodeAlive = !alertTimer.isExpired();
  if(nodeAlive != nodeAlive_) {
    nodeAlive_ = nodeAlive;
    const char* statusStr = nodeAlive_ ? STATUS_ONLINE : STATUS_OFFLINE;
    canHandler.serialPort.printf_P(PSTR("[CANB] %s is %s!\r\n"), MqttComBase::getClassId(), statusStr);
    static constexpr const uint8_t dataOutBufSize = 64;
    char dataOut[dataOutBufSize] = { '\0' };
    const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), STATUS_FRAME, statusStr);
    const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
    if(!dataOutValid) { return false; }
    MqttComBase::messageSend(dataOut);
  }
  runOta();
  return run();
}

void CanHandler::CanComBase::canFrameReceivedPriv(CanHandler::CanFrame& canFrame) {
  pingTimer.reload();
  alertTimer.reload();
  const uint16_t command = static_cast<uint16_t>(canFrame.cmd);
  switch(command) {
    case static_cast<uint16_t>(CanCmd::PING): {} break;
    case static_cast<uint16_t>(CanCmd::RESTART): {
      static constexpr const uint8_t dataOutBufSize = 64;
      char dataOut[dataOutBufSize] = { '\0' };
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), STATUS_FRAME, STATUS_RESTARTED);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return; }
      MqttComBase::messageSend(dataOut);
    } break;
    case static_cast<uint16_t>(CanCmd::FW_VERSION): {
      const uint16_t fwVersion =
        (static_cast<uint16_t>(canFrame.data[0]) << 0) |
        (static_cast<uint16_t>(canFrame.data[1]) << 8);
      const uint32_t gitHash = 
        (static_cast<uint32_t>(canFrame.data[2]) << 0) |
        (static_cast<uint16_t>(canFrame.data[3]) << 8) |
        (static_cast<uint16_t>(canFrame.data[4]) << 16) |
        (static_cast<uint16_t>(canFrame.data[5]) << 24);
      static constexpr const uint8_t dataOutBufSize = 96;
      char dataOut[dataOutBufSize] = { '\0' };
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), FW_VERSION_FRAME, fwVersion, gitHash);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return; }
      MqttComBase::messageSend(dataOut);
    } break;
    case static_cast<uint16_t>(CanCmd::BUTTON_EVENT): {
      const uint8_t buttonState = canFrame.data[0];
      static constexpr const uint8_t dataOutBufSize = 64;
      char dataOut[dataOutBufSize] = { '\0' };
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), BUTTON_FRAME, buttonState);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return; }
      MqttComBase::messageSend(dataOut);
    } break;
    case static_cast<uint16_t>(CanCmd::OTA_START):
    case static_cast<uint16_t>(CanCmd::OTA_SEND): {
      const Response response = static_cast<Response>(canFrame.data[0]);
      transferState = (response == Response::ACK) ? TransferState::STORE : TransferState::INVALID;
    } break;
    case static_cast<uint16_t>(CanCmd::OTA_END): {
      const Response response = static_cast<Response>(canFrame.data[0]);
      transferState = (response == Response::ACK) ? TransferState::VALID : TransferState::INVALID;
    } break;
    default: { canFrameReceived(canFrame); } break;
  }
}

bool CanHandler::CanComBase::begin() { return true; }

bool CanHandler::CanComBase::loop() { return true; }

void CanHandler::CanComBase::messageReceived(uint8_t* payload, uint32_t length) {
  JsonDocument cmdJson;
  DeserializationError deserializationError = deserializeJson(cmdJson, payload, length);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(!deSerResult) {
    canHandler.serialPort.printf_P(PSTR("[CANB] Deserialisation failed at %s: %s\r\n"),
      MqttComBase::getClassId(), reinterpret_cast<const char*>(deserializationError.f_str()));
    return;
  }
  JsonVariant fileJsonVar = cmdJson[F("File")];
  if(fileJsonVar.is<const char*>()) {
    const char* fileName = fileJsonVar.as<const char*>();
    const bool fileTransferStartResult = startOta(fileName);
    canHandler.serialPort.printf_P(PSTR("[CANB] File transfer starts to \"%s\":%s\r\n"),
      MqttComBase::getClassId(), fileTransferStartResult ? CanHandler::OK_STATE : CanHandler::ERR_STATE);
    if(!fileTransferStartResult) { transferState = TransferState::INVALID; }
    return;
  }
  JsonVariant commandJsonVar = cmdJson[F("Command")];
  JsonVariant dataJsonVar = cmdJson[F("Data")];
  if(commandJsonVar.is<uint16_t>() && dataJsonVar.is<const char*>()) {
    const uint16_t command = commandJsonVar.as<uint16_t>();
    const char* canDataStr = dataJsonVar.as<const char*>();
    if(canDataStr == nullptr) { return; }
    char* endPtr = nullptr;
    const uint64_t canData64 = std::strtoull(canDataStr, &endPtr, 16);
    if(*endPtr != '\0') { return; }
    uint8_t canData[8] = { 0 };
    memcpy(canData, &canData64, sizeof(canData));
    sendCanFrame(command, canData);
    return;
  }
}

uint32_t CanHandler::CanComBase::getCanId() const { return nodeCanId; }

bool CanHandler::CanComBase::sendCanFrame(CanCmd command, const uint8_t (&data)[8]) const {
  return sendCanFrame(static_cast<uint16_t>(command), data);
}

bool CanHandler::CanComBase::sendCanFrame(uint16_t command, const uint8_t (&data)[8]) const {
  CanFrame canFrame;
  canFrame.from = canHandler.localCanId;
  canFrame.to = nodeCanId;
  canFrame.cmd = command;
  memcpy(canFrame.data, data, sizeof(data));
  return canHandler.send(canFrame);
}

bool CanHandler::CanComBase::sendCanCmd(CanCmd command) const {
  return sendCanCmd(static_cast<uint16_t>(command));
}

bool CanHandler::CanComBase::sendCanCmd(uint16_t command) const {
  uint8_t data[8] = { 0 };
  return sendCanFrame(command, data);
}

bool CanHandler::CanComBase::startOta(const char* fileName) {
  if(fileName == nullptr) { return false; }
  const uint32_t fileNameLength = strnlen(fileName, sizeof(this->fileName));
  if(fileNameLength == 0 || fileNameLength >= sizeof(this->fileName)) { return false; }
  if(fileName[0] != '/') { return false; }
  memccpy(this->fileName, fileName, '\0', sizeof(this->fileName));
  if(!LittleFS.exists(this->fileName)) { return false; }
  if(receivedFile.name() != nullptr) { receivedFile.close(); }
  receivedFile = LittleFS.open(FPSTR(this->fileName), "r");
  if(receivedFile.name() == nullptr) {
    receivedFile.close();
    return false;
  }
  fileSize = receivedFile.size();
  if(fileSize == 0) { return false; }
  crc16.reset();
  frameNumber = 0;
  transferState = TransferState::START;
  return true;
}

void CanHandler::CanComBase::runOta() {
  if(otaTimeoutTimer.isExpired()) {
    transferState = TransferState::IDLE;
  }
  switch(transferState) {
    case TransferState::IDLE: { otaTimeoutTimer.reload(); } break;
    case TransferState::START: {
      const uint32_t remainingBytes = receivedFile.available();
      if(remainingBytes > 0U) {
        constexpr uint8_t bufferSize = 64U;
        uint8_t readBuffer[bufferSize] = { 0 };
        const uint8_t readLength = remainingBytes >= bufferSize ? bufferSize : remainingBytes;
        receivedFile.read(readBuffer, readLength);
        crc16.next(readBuffer, readLength);
      }
      else {
        receivedFile.seek(0, SeekSet);
        fileCrc = crc16.get();
        const uint8_t canData[8] = {
          static_cast<uint8_t>((storageNumber >> 0) & 0xFF),  // Lower byte.
          static_cast<uint8_t>((storageNumber >> 8) & 0xFF),  // Upper byte.
          static_cast<uint8_t>((fileSize >> 0) & 0xFF),       // Lowest byte.
          static_cast<uint8_t>((fileSize >> 8) & 0xFF),
          static_cast<uint8_t>((fileSize >> 16) & 0xFF),
          static_cast<uint8_t>((fileSize >> 24) & 0xFF),      // Highest byte.
          static_cast<uint8_t>((fileCrc >> 0) & 0xFF),        // Lower byte.
          static_cast<uint8_t>((fileCrc >> 8) & 0xFF),        // Upper byte.
        };
        const bool sendResult = sendCanFrame(CanCmd::OTA_START, canData);
        transferState = sendResult ? TransferState::START_ACK : TransferState::INVALID;
      }
    } break;
    case TransferState::START_ACK: {} break;
    case TransferState::STORE: {
      otaTimeoutTimer.reload();
      constexpr uint8_t pieceSize = 4U;
      const uint32_t remainingFileSize = receivedFile.available();
      if(remainingFileSize == 0U) {
        transferState = TransferState::END_ACK;
        break;
      }
      const uint8_t bytesNumber = remainingFileSize >= pieceSize ? pieceSize : remainingFileSize;
      uint8_t canData[8] = { 0 };
      receivedFile.read(canData, bytesNumber);
      canData[4] = static_cast<uint8_t>((frameNumber >> 0) & 0xFF);
      canData[5] = static_cast<uint8_t>((frameNumber >> 8) & 0xFF);
      canData[6] = static_cast<uint8_t>((frameNumber >> 16) & 0xFF);
      canData[7] = static_cast<uint8_t>((frameNumber >> 24) & 0xFF);
      frameNumber += bytesNumber;
      const bool sendResult = sendCanFrame(CanCmd::OTA_SEND, canData);
      transferState = sendResult ? TransferState::STORE_ACK : TransferState::INVALID;
    } break;
    case TransferState::STORE_ACK: {} break;
    case TransferState::END_ACK: {} break;
    case TransferState::VALID:
    case TransferState::INVALID: {
      receivedFile.close();
      frameNumber = 0;
      storageNumber = 0;
      fileSize = 0;
      crc16.reset();
      memset(fileName, '\0', sizeof(fileName));
      {
        const bool otaStatus = (transferState == TransferState::VALID);
        static constexpr const uint8_t dataOutBufSize = 64;
        char dataOut[dataOutBufSize] = { '\0' };
        const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), OTA_FRAME, reinterpret_cast<const char*>(otaStatus ? F("OK") : F("ERR")));
        const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
        if(dataOutValid) { MqttComBase::messageSend(dataOut); }
        canHandler.serialPort.printf_P(PSTR("[CANB] File transfer for \"%s\":%s\r\n"),
          MqttComBase::getClassId(), otaStatus ? CanHandler::OK_STATE : CanHandler::ERR_STATE);
      }
      transferState = TransferState::IDLE;
    } break;
  }
}
#endif