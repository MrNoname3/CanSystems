#include "canHandler.hpp"
#include <CAN.h>                              /// CAN controller library.
#include <ArduinoJson.h>                      /// Handle JSON files.
#include <cstdlib>

QueueHandle_t CanHandler::canRxQueue = nullptr;
const char CanHandler::OK_STATE[] PROGMEM                 = " [OK]";                    // OK status.
const char CanHandler::ERR_STATE[] PROGMEM                = " [ERR]";                   // Error status.
const char CanHandler::CAN_PREFIX[] PROGMEM               = "[CAN] ";

CanHandler::CanHandler(HardwareSerial& serial) : serialPort(serial) {}

bool CanHandler::begin(uint32_t canBaud) {
  serialPort.printf_P(PSTR("%sHardware init started...\r\n"), CAN_PREFIX);
  const bool canBeginResult = CAN.begin(canBaud) == 1;
  serialPort.printf_P(PSTR("%sInit:%s\r\n"), CAN_PREFIX, (canBeginResult ? OK_STATE : ERR_STATE));
  if(!canBeginResult) { return false; }
  CAN.onReceive(rxInterrupt);
  { // Calculate the mask to ignore the upper bits of the extended CAN ID and only consider the lower 10 bits.
    const uint16_t deviceAddress = localCanId;
    const uint32_t mask = 0x3FFU;                   // Mask for lower 10 bits (0b1111111111).
    const uint32_t id = deviceAddress & mask;       // Calculate the ID using the device's local address.
    const bool setFilterResult = CAN.filterExtended(id, mask) == 1;
    serialPort.printf_P(PSTR("%sFilter:%s\r\n"), CAN_PREFIX, (setFilterResult ? OK_STATE : ERR_STATE));
    if(!setFilterResult) { return false; }
  }
  canRxQueue = xQueueCreate(canRxQueueSize, sizeof(CanFrame));  // Create FIFO queue for RX CAN packets.
  const bool rxQueueResult = canRxQueue != nullptr;             // Check queue creation.
  canTxQueue = xQueueCreate(canTxQueueSize, sizeof(CanFrame));  // Create FIFO queue for TX CAN packets.
  const bool txQueueResult = canTxQueue != nullptr;
  serialPort.printf_P(PSTR("%sCreating queues:%s\r\n"), CAN_PREFIX, (rxQueueResult && txQueueResult ? OK_STATE : ERR_STATE));
  if(!rxQueueResult || !txQueueResult) { return false; }
  serialPort.printf_P(PSTR("%sInit registered objects:\r\n"), CAN_PREFIX);
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

const char CanHandler::CanComBase::CAN_BASE_PREFIX[] PROGMEM            = "[CANB] ";
const char CanHandler::CanComBase::STATUS_ONLINE[] PROGMEM              = "ONLINE";
const char CanHandler::CanComBase::STATUS_OFFLINE[] PROGMEM             = "OFFLINE";
const char CanHandler::CanComBase::STATUS_RESTARTED[] PROGMEM           = "RESTARTED";
const char CanHandler::CanComBase::STATUS_FRAME[] PROGMEM = {
  "{"
    "\"Time\":\"%s\","
    "\"Status\":\"%s\""
  "}"
};
const char CanHandler::CanComBase::BUTTON_FRAME[] PROGMEM = {
  "{"
    "\"Time\":\"%s\","
    "\"Button\":%hu"
  "}"
};
const char CanHandler::CanComBase::FW_VERSION_FRAME[] PROGMEM = {
  "{"
    "\"Time\":\"%s\","
    "\"Firmware\":%hu,"
    "\"GitHash\":\"%x\""
  "}"
};
const char CanHandler::CanComBase::OTA_FRAME[] PROGMEM = {
  "{"
    "\"Time\":\"%s\","
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
    canHandler.serialPort.printf_P(PSTR("%s%s is %s!\r\n"), CAN_BASE_PREFIX, MqttComBase::getClassId(), statusStr);
    static constexpr const uint8_t dataOutBufSize = 64;
    char dataOut[dataOutBufSize] = { '\0' };
    const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), STATUS_FRAME, MqttComBase::getIsoTime(), statusStr);
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
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), STATUS_FRAME, MqttComBase::getIsoTime(), STATUS_RESTARTED);
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
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), FW_VERSION_FRAME,
        MqttComBase::getIsoTime(), fwVersion, gitHash);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return; }
      MqttComBase::messageSend(dataOut);
    } break;
    case static_cast<uint16_t>(CanCmd::BUTTON_EVENT): {
      const uint8_t buttonState = canFrame.data[0];
      static constexpr const uint8_t dataOutBufSize = 64;
      char dataOut[dataOutBufSize] = { '\0' };
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), BUTTON_FRAME,
        MqttComBase::getIsoTime(), buttonState);
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
    canHandler.serialPort.printf_P(PSTR("%sDeserialisation failed at %s: %s\r\n"),
      CAN_BASE_PREFIX, MqttComBase::getClassId(), deserializationError.f_str());
    return;
  }
  JsonVariant fileJsonVar = cmdJson[F("File")];
  if(fileJsonVar.is<const char*>()) {
    const char* fileName = fileJsonVar.as<const char*>();
    const bool fileTransferStartResult = startOta(fileName);
    canHandler.serialPort.printf_P(PSTR("%sFile transfer starts to \"%s\":%s\r\n"), CAN_BASE_PREFIX,
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

const uint32_t CanHandler::CanComBase::getCanId() const { return nodeCanId; }

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
      transferState = sendResult ? TransferState::STORE_ACK : transferState = TransferState::INVALID;
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
        const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), OTA_FRAME,
          MqttComBase::getIsoTime(), otaStatus ? F("OK") : F("ERR"));
        const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
        if(dataOutValid) { MqttComBase::messageSend(dataOut); }
        canHandler.serialPort.printf_P(PSTR("%sFile transfer for \"%s\":%s\r\n"), CAN_BASE_PREFIX,
          MqttComBase::getClassId(), otaStatus ? CanHandler::OK_STATE : CanHandler::ERR_STATE);
      }
      transferState = TransferState::IDLE;
    } break;
  }
}