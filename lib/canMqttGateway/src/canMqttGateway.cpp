#include "canMqttGateway.hpp"
#include <ctype.h>

CanOta::CanOta(CanMqttGateway& canMqttGateway) :
  canMqttGateway(canMqttGateway),
  receivedFile(),
  frameNumber(0U),
  storageNumber(0U),
  fileSize(0U),
  transferState(TransferState::IDLE),
  otaTimeoutTimer(0U),
  fileNamePtr(nullptr)
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
  if(fileName[0] != '/') {
    otaStartErrState.setError(OtaStartError::FILE_LOCATION_INVALID);
    return otaStartErrState.getRawErrorState();
  }
  fileNamePtr = fileName;
  if(receivedFile) {
    receivedFile.close();
  }
  receivedFile = LittleFS.open(fileNamePtr, FILE_READ);
  if(!receivedFile) {
    otaStartErrState.setError(OtaStartError::FILE_OPEN_FAILED);
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

void CanOta::handleOtaCanFrames(const CanHandler::CanFrame& canFrame) { // NOLINT(readability-convert-member-functions-to-static)
  const uint16_t cmd = static_cast<uint16_t>(canFrame.cmd);
  const CanHandler::Response response = static_cast<CanHandler::Response>(canFrame.data[0]);
  if((cmd == static_cast<uint16_t>(CanCmd::OTA_START)) || (cmd == static_cast<uint16_t>(CanCmd::OTA_SEND))) {
    transferState = (response == CanHandler::Response::ACK) ? TransferState::STORE : TransferState::INVALID;
    return;
  }
  if(cmd == static_cast<uint16_t>(CanCmd::OTA_END)) {
    transferState = (response == CanHandler::Response::ACK) ? TransferState::VALID : TransferState::INVALID;
    return;
  }
}

void CanOta::runOta() {
  const uint32_t actualTime = millis();
  if(Time::hasElapsed(actualTime, otaTimeoutTimer, otaTimeoutTime)) {
    // Route an in-progress timeout through INVALID so the existing cleanup runs: the file gets
    // closed, the counters reset, and the {"OTA":"[ERR]"} status reaches the server. Jumping
    // straight to IDLE used to skip all of that. VALID/INVALID are excluded: they are processed
    // within the same pass, and a timeout must not overwrite an already-arrived final ACK.
    const bool otaInProgress = (transferState == TransferState::START) ||
                               (transferState == TransferState::STORE) ||
                               (transferState == TransferState::WAIT_FOR_ACK);
    if(otaInProgress) {
      Logger::get().printf_P(PSTR("[CAN] OTA timeout for \"%s\"!\r\n"), canMqttGateway.getSubtopic());
      transferState = TransferState::INVALID;
    }
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
          static_cast<uint8_t>(storageNumber & 0xFF),
          static_cast<uint8_t>((storageNumber >> 8U) & 0xFF),
          static_cast<uint8_t>(fileSize & 0xFF),
          static_cast<uint8_t>((fileSize >> 8U) & 0xFF),
          static_cast<uint8_t>((fileSize >> 16U) & 0xFF),
          static_cast<uint8_t>((fileSize >> 24U) & 0xFF),
          static_cast<uint8_t>(fileCrc & 0xFF),
          static_cast<uint8_t>((fileCrc >> 8U) & 0xFF)
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
      canData[4] = static_cast<uint8_t>(frameNumber & 0xFF);
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
        Logger::get().printf_P(PSTR("[CAN] File transfer to \"%s\": %s\r\n"),
          canMqttGateway.getSubtopic(), Str::getStateStr(otaStatus));
        char dataOut[otaFrameBufSize] = {'\0'};
        const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), otaFrame, Str::getStateStr(otaStatus));
        const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
        if(dataOutValid) {
          (void)canMqttGateway.sendOtaStatusMessage(dataOut);
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
      fileNamePtr = nullptr;
    } break;
  }
}

CanMqttGateway::CanMqttGateway(CanHandler& canHandler, uint16_t clientCanId, Connectivity& connectivity, const char* subTopic, const char* fwFileName) :
  CanBase(canHandler, clientCanId),
  MqttBase(connectivity, subTopic),
  canOta(*this),
  clientPingTimer(0U),
  clientOfflineTimer(0U),
  clientOnline(true),
  fwFileNamePtr(fwFileName)
{
  if(fwFileNamePtr != nullptr) {
    OtaRegistry::add(*this);
  }
}

bool CanMqttGateway::startOta(const char* fileName) { // NOLINT(readability-convert-member-functions-to-static)
  const uint8_t otaStartResultCode = canOta.startOta(fileName);
  const bool fileTransferStartResult = (otaStartResultCode == 0U);
  Logger::get().printf_P(PSTR("[CAN] File transfer starts to \"%s\": %s\r\n"),
    MqttBase::getSubtopic(), Str::getStateStr(fileTransferStartResult));
  if(!fileTransferStartResult) {
    Logger::get().printf_P(Str::getErrCodeFmt(), otaStartResultCode);
  }
  return fileTransferStartResult;
}

bool CanMqttGateway::isOtaInProgress() const {
  return canOta.isOtaInProgress();
}

bool CanMqttGateway::sendOtaStatusMessage(const char* payload) { // NOLINT(readability-convert-member-functions-to-static)
  const char* subSubTopic = canOtaTopic + (MqttTopics::getSenderTopicBufSize() - 1U);
  return MqttBase::sendSubtopicMessage(subSubTopic, payload);
}

void CanMqttGateway::buildCanTopics() {
  if(canTopicsBuilt) { return; }
  const char* sender   = MqttBase::getSenderTopicStr();
  const char* sub      = MqttBase::getSubtopic();
  const char* client   = MqttBase::getClientNameStr();
  if(sender == nullptr || sub == nullptr || client == nullptr) { return; }
  if(sender[0] == '\0' || sub[0] == '\0' || client[0] == '\0') { return; }

  // Build base: senderTopic + subtopic + "/" — used as %s argument for the topic templates.
  char base[MqttTopics::getSenderTopicBufSize() + MqttBase::getSubtopicSize()] = {};
  strlcpy(base, sender, sizeof(base));
  strlcat(base, sub, sizeof(base));
  strlcat(base, "/", sizeof(base));

  (void)snprintf_P(canAvailTopic, sizeof(canAvailTopic), MqttTopics::getMqttAvailTopic(), base);
  (void)snprintf_P(canInfoTopic,  sizeof(canInfoTopic),  MqttTopics::getMqttInfoTopic(),  base);
  (void)snprintf_P(canOtaTopic,    sizeof(canOtaTopic),    PSTR("%s%s"), base, canOtaSuffix);
  (void)snprintf_P(canButtonTopic, sizeof(canButtonTopic), PSTR("%s%s"), base, canButtonSuffix);

  (void)snprintf(canDeviceId, sizeof(canDeviceId), "%s_%s", client, sub);

  // canDeviceName = UPPERCASE(subtopic) + " " + last 3 MAC byte pairs.
  // senderTopic layout: "iot/dtos/<12hex>/" — last 3 pairs start at offset (prefix + first 3 pairs).
  static constexpr uint8_t mac3Offset = 9U + MqttTopics::getMacHexLen() / 2U;
  uint8_t i = 0U;
  while((i < sizeof(canDeviceName) - 8U) && (sub[i] != '\0')) {
    canDeviceName[i] = static_cast<char>(toupper(static_cast<unsigned char>(sub[i])));
    ++i;
  }
  if(i < sizeof(canDeviceName) - 1U) {
    canDeviceName[i++] = ' ';
    (void)snprintf(canDeviceName + i, 7U, "%.6s", sender + mac3Offset);
  }

  canTopicsBuilt = true;
}

bool CanMqttGateway::init() {
  buildCanTopics();
  (void)sendCanFrame(CanCmd::FW_VERSION);
  clientPingTimer = millis();
  // clientOfflineTimer is intentionally NOT reset here so handlePing() continues to base
  // its decision on the last real CAN activity, not on a forced timestamp.
  const char* availSubtopic = canAvailTopic + (MqttTopics::getSenderTopicBufSize() - 1U);
  (void)MqttBase::sendRetainedSubtopic(availSubtopic, MqttTopics::availOfflinePayload);
  clientOnline = false;
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
    clientPingTimer = actualTime;
  }
  const bool clientOnlineActual = !Time::hasElapsed(actualTime, clientOfflineTimer, clientOfflineTime);
  if(clientOnline != clientOnlineActual) {
    clientOnline = clientOnlineActual;
    Logger::get().printf_P(PSTR("[CAN] %s is %s!\r\n"), MqttBase::getSubtopic(),
      Str::getOnlineStateStr(clientOnline));
    const char* availSubtopic = canAvailTopic + (MqttTopics::getSenderTopicBufSize() - 1U);
    (void)MqttBase::sendRetainedSubtopic(availSubtopic,
      clientOnline ? MqttTopics::availOnlinePayload : MqttTopics::availOfflinePayload);
  }
}

void CanMqttGateway::messageArrivedCallback(JsonDocument& payloadJson) { // NOLINT(readability-convert-member-functions-to-static)
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
  switch(static_cast<uint16_t>(canFrame.cmd)) {
    case static_cast<uint16_t>(CanCmd::PING): {} break;
    case static_cast<uint16_t>(CanCmd::RESTART): {
      (void)sendCanFrame(CanCmd::FW_VERSION);
      // Publish offline then online so brief restarts are visible in HA connection history,
      // even if the device came back before the ping timeout would have caught the outage.
      // clientOnline is forced true so handlePing() does not publish a duplicate online.
      const char* availSubtopic = canAvailTopic + (MqttTopics::getSenderTopicBufSize() - 1U);
      (void)MqttBase::sendRetainedSubtopic(availSubtopic, MqttTopics::availOfflinePayload);
      (void)MqttBase::sendRetainedSubtopic(availSubtopic, MqttTopics::availOnlinePayload);
      clientOnline = true;
    } break;
    case static_cast<uint16_t>(CanCmd::FW_VERSION): {
      const uint16_t fwVersion =
        static_cast<uint16_t>(canFrame.data[0]) |
        (static_cast<uint16_t>(canFrame.data[1]) << 8U);
      const uint32_t gitHash =
        static_cast<uint32_t>(canFrame.data[2]) |
        (static_cast<uint32_t>(canFrame.data[3]) << 8U) |
        (static_cast<uint32_t>(canFrame.data[4]) << 16U) |
        (static_cast<uint32_t>(canFrame.data[5]) << 24U);
      const uint8_t gitDirty = canFrame.data[6];
      (void)snprintf(canSwVersion, sizeof(canSwVersion), "%hu (%08x)", fwVersion, gitHash);
      char dataOut[MqttTopics::getInfoPayloadBufSize()] = { '\0' };
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), MqttTopics::getMqttInfoPayload(), fwVersion, gitHash, gitDirty, 255U);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(dataOutValid) {
        const char* infoSubtopic = canInfoTopic + (MqttTopics::getSenderTopicBufSize() - 1U);
        (void)MqttBase::sendRetainedSubtopic(infoSubtopic, dataOut);
      }
      if(!clientOnline) {
        const char* availSubtopic = canAvailTopic + (MqttTopics::getSenderTopicBufSize() - 1U);
        (void)MqttBase::sendRetainedSubtopic(availSubtopic, MqttTopics::availOnlinePayload);
        clientOnline = true;
      }
      (void)publishDiscovery();
    } break;
    case static_cast<uint16_t>(CanCmd::BUTTON_EVENT): {
      const uint8_t buttonState = canFrame.data[0];
      char dataOut[buttonFrameBufSize] = {'\0'};
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), buttonFrame, buttonState);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return; }
      const char* subSubTopic = canButtonTopic + (MqttTopics::getSenderTopicBufSize() - 1U);
      (void)MqttBase::sendSubtopicMessage(subSubTopic, dataOut);
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