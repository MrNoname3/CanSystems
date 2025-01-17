#include "canMqttGateway.hpp"

const char CanOta::otaFrame[] PROGMEM = {
  "{"
    "\"OTA\":\"%s\""
  "}"
};

CanOta::CanOta(CanMqttGateway& canMqttGateway) :
  canMqttGateway(canMqttGateway),
  receivedFile(),
  frameNumber(0U),
  storageNumber(0U),
  fileSize(0U),
  transferState(TransferState::IDLE),
  crc16(),
  otaTimeoutTimer(0U),
  fileNameLocal{'\0'}
{}

CanOta::~CanOta() {
  if(receivedFile){
    receivedFile.close();
  }
}

CanOta::OtaStartErrorType CanOta::startOta(const char* fileName, uint16_t storageNumber) {
  ErrorState<OtaStartError, OtaStartErrorType> otaStartErrState;
  if(fileName == nullptr) {
    otaStartErrState.setError(OtaStartError::FILE_NAME_NULLPTR);
    return otaStartErrState.getRawErrorState();
  }
  const char* subtopic = canMqttGateway.getSubtopic();
  if(subtopic == nullptr) {
    otaStartErrState.setError(OtaStartError::SUBTOPIC_NULLPTR);
    return otaStartErrState.getRawErrorState();
  }
  memset(fileNameLocal, '\0', sizeof(fileNameLocal));
  const int32_t fileNameStrSize = snprintf_P(fileNameLocal, sizeof(fileNameLocal), fileName, subtopic);
  const bool fileNameStrValid = (fileNameStrSize >= 0 && fileNameStrSize < static_cast<int32_t>(sizeof(fileNameLocal)));
  if(!fileNameStrValid) {
    otaStartErrState.setError(OtaStartError::FILE_NAME_STR_INVALID);
    transferState = TransferState::INVALID;
    return otaStartErrState.getRawErrorState();
  }
  if(strlen(fileNameLocal) == 0U) {
    otaStartErrState.setError(OtaStartError::FILE_NAME_STR_EMPTY);
    return otaStartErrState.getRawErrorState();
  }
  if(!LittleFS.exists(fileNameLocal)) {
    otaStartErrState.setError(OtaStartError::FILE_NOT_EXISTS);
    return otaStartErrState.getRawErrorState();
  }
  if(receivedFile) {
    receivedFile.close();
  }
  receivedFile = LittleFS.open(fileNameLocal, FILE_READ);
  if(!receivedFile) {
    otaStartErrState.setError(OtaStartError::CANNOT_OPEN_FILE);
    return otaStartErrState.getRawErrorState();
  }
  fileSize = receivedFile.size();
  if(fileSize == 0U) {
    otaStartErrState.setError(OtaStartError::FILE_EMPTY);
    transferState = TransferState::INVALID;
    receivedFile.close();
    return otaStartErrState.getRawErrorState();
  }
  frameNumber = 0U;
  this->storageNumber = storageNumber;
  transferState = TransferState::START;
  crc16.reset();
  otaTimeoutTimer = millis();
  return otaStartErrState.getRawErrorState();
}

void CanOta::handleOtaCanFrames(const CanHandler::CanFrame& canFrame) {
  const uint16_t command = static_cast<uint16_t>(canFrame.cmd);
  const CanHandler::Response response = static_cast<CanHandler::Response>(canFrame.data[0]);
  if((command == static_cast<uint16_t>(CanCmd::OTA_START)) || (command == static_cast<uint16_t>(CanCmd::OTA_SEND))) {
    transferState = (response == CanHandler::Response::ACK) ? TransferState::STORE : TransferState::INVALID;
    return;
  }
  if(command == static_cast<uint16_t>(CanCmd::OTA_END)) {
    transferState = (response == CanHandler::Response::ACK) ? TransferState::VALID : TransferState::INVALID;
    return;
  }
}

void CanOta::runOta() {
  const uint32_t actualTime = millis();
  if(Time::hasElapsed(actualTime, otaTimeoutTimer, otaTimeoutTime)) {
    transferState = TransferState::IDLE;
  }

  switch(transferState) {
    case TransferState::IDLE: {
      otaTimeoutTimer = actualTime;
    } break;
    case TransferState::WAIT_FOR_ACK: {} break;
    case TransferState::START: {
      const uint32_t remainingBytes = receivedFile.available();
      if(remainingBytes > 0U) {
        uint8_t readBuffer[readBufferSize] = {0U};
        const uint8_t readLength = (remainingBytes >= readBufferSize) ? readBufferSize : remainingBytes;
        receivedFile.read(readBuffer, readLength);
        crc16.next(readBuffer, readLength);
      } else {
        receivedFile.seek(0U, SeekSet);
        const uint16_t fileCrc = crc16.get();
        const uint8_t canData[8] = {
          static_cast<uint8_t>((storageNumber >> 0U) & 0xFF),
          static_cast<uint8_t>((storageNumber >> 8U) & 0xFF),
          static_cast<uint8_t>((fileSize >> 0U) & 0xFF),
          static_cast<uint8_t>((fileSize >> 8U) & 0xFF),
          static_cast<uint8_t>((fileSize >> 16U) & 0xFF),
          static_cast<uint8_t>((fileSize >> 24U) & 0xFF),
          static_cast<uint8_t>((fileCrc >> 0U) & 0xFF), 
          static_cast<uint8_t>((fileCrc >> 8U) & 0xFF),
        };
        transferState = canMqttGateway.sendCanFrame(CanCmd::OTA_START, canData) ?
          TransferState::WAIT_FOR_ACK : TransferState::INVALID;
      }
    } break;
    case TransferState::STORE: {
      otaTimeoutTimer = actualTime;
      const uint32_t remainingFileSize = receivedFile.available();
      if(remainingFileSize == 0U) {
        transferState = TransferState::WAIT_FOR_ACK;
        break;
      }
      const uint8_t bytesNumber = (remainingFileSize >= filePieceSize) ? filePieceSize : remainingFileSize;
      uint8_t canData[8] = {0U};
      receivedFile.read(canData, bytesNumber);
      canData[4] = static_cast<uint8_t>((frameNumber >> 0U) & 0xFF);
      canData[5] = static_cast<uint8_t>((frameNumber >> 8U) & 0xFF);
      canData[6] = static_cast<uint8_t>((frameNumber >> 16U) & 0xFF);
      canData[7] = static_cast<uint8_t>((frameNumber >> 24U) & 0xFF);
      frameNumber += bytesNumber;
      transferState = canMqttGateway.sendCanFrame(CanCmd::OTA_SEND, canData) ?
        TransferState::WAIT_FOR_ACK : TransferState::INVALID;
    } break;
    case TransferState::VALID:
    case TransferState::INVALID: {
      {
        const bool otaStatus = (transferState == TransferState::VALID);
        char dataOut[otaFrameBufSize] = {'\0'};
        const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), otaFrame, Str::getStateStr(otaStatus));
        const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
        if(dataOutValid) {
          (void)canMqttGateway.sendMessage(dataOut);
        }
      }
      if(receivedFile){
        receivedFile.close();
      }
      frameNumber = 0U;
      storageNumber = 0U;
      fileSize = 0U;
      transferState = TransferState::IDLE;
      crc16.reset();
      memset(fileNameLocal, '\0', sizeof(fileNameLocal));
    } break;
  }
}

const char CanMqttGateway::statusOnline[] PROGMEM = "ONLINE";
const char CanMqttGateway::statusOffline[] PROGMEM = "OFFLINE";
const char CanMqttGateway::statusRestarted[] PROGMEM = "RESTARTED";
const char CanMqttGateway::statusFrame[] PROGMEM = {
  "{"
    "\"Status\":\"%s\""
  "}"
};
const char CanMqttGateway::buttonFrame[] PROGMEM = {
  "{"
    "\"Button\":%hu"
  "}"
};
const char CanMqttGateway::buildInfoFrame[] PROGMEM = {
  "{"
    "\"Firmware\":%hu,"
    "\"GitHash\":\"%x\""
  "}"
};

CanMqttGateway::CanMqttGateway(CanHandler& canHandler, uint16_t clientCanId, Connectivity& connectivity, const char* subTopic) :
  CanBase(canHandler, clientCanId),
  MqttBase(connectivity, subTopic),
  canOta(*this),
  clientPingTimer(0U),
  clientOfflineTimer(0U),
  clientOnline(true)
{}

bool CanMqttGateway::init() {
  (void)sendCanFrame(CanCmd::PING);
  clientPingTimer = clientOfflineTimer = millis();
  return initLocal();
}

bool CanMqttGateway::run() {
  handlePing();
  canOta.runOta();
  return runLocal();
}

void CanMqttGateway::handlePing() {
  const uint32_t actualTime = millis();
  if(Time::hasElapsed(actualTime, clientPingTimer, clientPingTime)) {
    (void)sendCanFrame(CanCmd::PING);
    clientPingTimer = millis();
  }
  const bool clientOnlineActual = !Time::hasElapsed(actualTime, clientOfflineTimer, clientOfflineTime);
  if(clientOnline != clientOnlineActual) {
    clientOnline = clientOnlineActual;
    Logger::get().printf_P(PSTR("[CAN] %s is %s!\r\n"), MqttBase::getSubtopic(), clientOnline ? statusOnline : statusOffline);
    char dataOut[statusFrameBufSize] = {'\0'};
    const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), statusFrame, clientOnline ? statusOnline : statusOffline);
    const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
    if(dataOutValid) {
      (void)MqttBase::sendMessage(dataOut);
    }
  }
}

void CanMqttGateway::messageArrivedCallback(JsonDocument& payloadJson) {
  JsonVariant fileJsonVar = payloadJson[F("File")];
  if(fileJsonVar.is<const char*>()) {
    const char* fileName = fileJsonVar.as<const char*>();
    const uint16_t otaStartResultCode = canOta.startOta(fileName);
    const bool fileTransferStartResult = (otaStartResultCode == 0U);
    Logger::get().printf_P(PSTR("[CAN] File transfer starts to \"%s\":%s\r\n"),
      MqttBase::getSubtopic(), Str::getStateStr(fileTransferStartResult));
    if(!fileTransferStartResult) {
      Logger::get().printf_P(PSTR("  Code: %hu\r\n"), otaStartResultCode);
    }
    return;
  }

  JsonVariant commandJsonVar = payloadJson[F("Command")];
  JsonVariant dataJsonVar = payloadJson[F("Data")];
  if(commandJsonVar.is<uint16_t>() && dataJsonVar.is<const char*>()) {
    const uint16_t command = commandJsonVar.as<uint16_t>();
    const char* canDataStr = dataJsonVar.as<const char*>();
    if(canDataStr == nullptr) { return; }
    char* endPtr = nullptr;
    const uint64_t canData64 = std::strtoull(canDataStr, &endPtr, 16);
    if(*endPtr != '\0') { return; }
    uint8_t canData[8] = {0U};
    memcpy(canData, &canData64, sizeof(canData));
    (void)sendCanFrame(command, canData);
    return;
  }

  processMessageArrived(payloadJson);
}

void CanMqttGateway::canFrameArrivedCallback(const CanHandler::CanFrame& canFrame) {
  clientPingTimer = clientOfflineTimer = millis();
  const uint16_t command = static_cast<uint16_t>(canFrame.cmd);
  switch(command) {
    case static_cast<uint16_t>(CanCmd::PING): {} break;
    case static_cast<uint16_t>(CanCmd::RESTART): {
      char dataOut[statusFrameBufSize] = {'\0'};
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), statusFrame, statusRestarted);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return; }
      (void)MqttBase::sendMessage(dataOut);
    } break;
    case static_cast<uint16_t>(CanCmd::FW_VERSION): {
      const uint16_t fwVersion =
        (static_cast<uint16_t>(canFrame.data[0]) << 0U) |
        (static_cast<uint16_t>(canFrame.data[1]) << 8U);
      const uint32_t gitHash = 
        (static_cast<uint32_t>(canFrame.data[2]) << 0U) |
        (static_cast<uint16_t>(canFrame.data[3]) << 8U) |
        (static_cast<uint16_t>(canFrame.data[4]) << 16U) |
        (static_cast<uint16_t>(canFrame.data[5]) << 24U);
      char dataOut[buildInfoFrameBufSize] = { '\0' };
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), buildInfoFrame, fwVersion, gitHash);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return; }
      (void)MqttBase::sendMessage(dataOut);
    } break;
    case static_cast<uint16_t>(CanCmd::BUTTON_EVENT): {
      const uint8_t buttonState = canFrame.data[0];
      char dataOut[buttonFrameBufSize] = {'\0'};
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), buttonFrame, buttonState);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return; }
      (void)MqttBase::sendMessage(dataOut);
    } break;
    case static_cast<uint16_t>(CanCmd::OTA_START):
    case static_cast<uint16_t>(CanCmd::OTA_SEND):
    case static_cast<uint16_t>(CanCmd::OTA_END): {
      canOta.handleOtaCanFrames(canFrame);
    } break;
    default: {
      processCanFrameArrived(canFrame);
    } break;
  }
}