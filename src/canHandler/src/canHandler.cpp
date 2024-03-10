#ifdef PROJECT_CAN
#include "canHandler.hpp"
#include <CAN.h>                              /// CAN controller library.
#include <ArduinoJson.h>                      /// Handle JSON files.
#include "../../crc16/src/crc16.hpp"

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

//////////////////// -- CanFileTransfer class-- ////////////////////

CanHandler::CanFileTransfer::CanFileTransfer(const char* fileName) :
  receivedFile(),
  firstFrame(true),
  frameNumber(0),
  storageNumber(0)
{
  if(fileName == nullptr) { return; }
  memccpy(this->fileName, fileName, '\0', sizeof(this->fileName));
  receivedFile = LittleFS.open(FPSTR(this->fileName), "r");
  if(!receivedFile) {
    receivedFile.close();
    return;
  }
  fileSize = receivedFile.size();
  Crc16 crc16;
  while(receivedFile.available() > 0) { crc16.next(receivedFile.read()); }
  fileCrc = crc16.get();
}

CanHandler::CanFileTransfer::~CanFileTransfer() {
  receivedFile.close();
}

bool CanHandler::CanFileTransfer::getNextFrame(uint8_t (&dataFrame)[8]) {
  if(fileName == nullptr) { return false; }
  if(fileSize == 0) { return false; }
  if(!receivedFile) { return false; }
  if(firstFrame) {
    dataFrame[0] = static_cast<uint8_t>((fileCrc >> 0) & 0xFF);       // Lower byte.
    dataFrame[1] = static_cast<uint8_t>((fileCrc >> 8) & 0xFF);       // Upper byte.
    dataFrame[2] = static_cast<uint8_t>((fileSize >> 0) & 0xFF);      // Lowest byte.
    dataFrame[3] = static_cast<uint8_t>((fileSize >> 8) & 0xFF);
    dataFrame[4] = static_cast<uint8_t>((fileSize >> 16) & 0xFF);
    dataFrame[5] = static_cast<uint8_t>((fileSize >> 24) & 0xFF);     // Highest byte.
    dataFrame[6] = static_cast<uint8_t>((storageNumber >> 0) & 0xFF); // Lower byte.
    dataFrame[7] = static_cast<uint8_t>((storageNumber >> 8) & 0xFF); // Upper byte.
    firstFrame = false;
    return true;
  }
  constexpr uint8_t pieceSize = 7U;
  const uint32_t remainingFileSize = receivedFile.available();
  const uint8_t bytesNumber = remainingFileSize >= pieceSize ? pieceSize : remainingFileSize;
  if(remainingFileSize == 0) { return false; }
  receivedFile.readBytes(reinterpret_cast<char*>(&dataFrame), bytesNumber);
  dataFrame[7] = frameNumber;
  frameNumber++;
  return true;
}

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
  canFileTransfer(nullptr)
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
      const bool fileTransferResult = sendFilePiece(CanCmd::OTA_START);
      if(!fileTransferResult) {
        canHandler.serialPort.printf_P(PSTR("%sGetting file piece failed at %s!\r\n"),
          CAN_BASE_PREFIX, MqttComBase::getClassId());
      }
    } break;
    case static_cast<uint16_t>(CanCmd::OTA_END): {
      const bool otaStatus = static_cast<bool>(canFrame.data[0]);
      static constexpr const uint8_t dataOutBufSize = 64;
      char dataOut[dataOutBufSize] = { '\0' };
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), OTA_FRAME,
        MqttComBase::getIsoTime(), otaStatus ? CanHandler::OK_STATE : CanHandler::ERR_STATE);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return; }
      MqttComBase::messageSend(dataOut);
      if(canFileTransfer == nullptr) { return; }
      delete canFileTransfer;
      canHandler.serialPort.printf_P(PSTR("%sFile transfer for %s:%s!\r\n"), CAN_BASE_PREFIX,
        MqttComBase::getClassId(), otaStatus ? CanHandler::OK_STATE : CanHandler::ERR_STATE);
    } break;
    default: { canFrameReceived(canFrame); } break;
  }
}

bool CanHandler::CanComBase::begin() { return true; }

bool CanHandler::CanComBase::loop() { return true; }

void CanHandler::CanComBase::messageReceived(uint8_t* payload, uint32_t length) {
  StaticJsonDocument<64> cmdJson;
  DeserializationError deserializationError = deserializeJson(cmdJson, payload, length);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(!deSerResult) {
    canHandler.serialPort.printf_P(PSTR("%sDeserialisation failed at %s: %s\r\n"),
      CAN_BASE_PREFIX, MqttComBase::getClassId(), deserializationError.f_str());
    return;
  }
  const bool isFileMsg = cmdJson.containsKey(F("File"));
  if(isFileMsg) {
    const char* fileName = cmdJson[F("File")].as<const char*>();
    if(canFileTransfer != nullptr) { return; }
    if(fileName == nullptr) { return; }
    canFileTransfer = new CanFileTransfer(fileName);
    const bool fileTransferStartResult = sendFilePiece(CanCmd::OTA_START);
    canHandler.serialPort.printf_P(PSTR("%sFile transfer start to %s: %s\r\n"), CAN_BASE_PREFIX,
      MqttComBase::getClassId(), fileTransferStartResult ? CanHandler::OK_STATE : CanHandler::ERR_STATE);
    return;
  }
  const bool isCanMsg = cmdJson.containsKey(F("Command")) && cmdJson.containsKey(F("Data"));
  if(isCanMsg) {
    const uint16_t command = cmdJson[F("Command")].as<uint16_t>();
    const uint64_t canData64 = cmdJson[F("Data")].as<uint64_t>();
    uint8_t canData[8] = { 0 };
    memcpy(canData, &canData64, sizeof(canData));
    sendCanFrame(command, canData);
    return;
  }
}

bool CanHandler::CanComBase::sendFilePiece(CanCmd command) {
  if(canFileTransfer == nullptr) { return false; }
  uint8_t canData[8] = { 0 };
  const bool dataValid = canFileTransfer->getNextFrame(canData);
  if(!dataValid) { return false; }
  sendCanFrame(command, canData);
  return true;
}

const uint32_t CanHandler::CanComBase::getCanId() const { return nodeCanId; }

void CanHandler::CanComBase::sendCanFrame(CanCmd command, const uint8_t (&data)[8]) const {
  sendCanFrame(static_cast<uint16_t>(command), data);
}

void CanHandler::CanComBase::sendCanFrame(uint16_t command, const uint8_t (&data)[8]) const {
  CanFrame canFrame;
  canFrame.from = localCanId;
  canFrame.to = nodeCanId;
  canFrame.cmd = command;
  memcpy(canFrame.data, data, sizeof(data));
  canHandler.send(canFrame);
}

void CanHandler::CanComBase::sendCanCmd(CanCmd command) const {
  sendCanCmd(static_cast<uint16_t>(command));
}

void CanHandler::CanComBase::sendCanCmd(uint16_t command) const {
  uint8_t data[8] = { 0 };
  sendCanFrame(command, data);
}
#endif // PROJECT_CAN