#include "canMqttGateway.hpp"
#include "bootProgress.hpp"                                        /// BootStage::Unknown for sub-devices.
#include <ctype.h>

CanOta::CanOta(CanMqttGateway& canMqttGateway) :
  canMqttGateway(canMqttGateway),
  receivedFile(),
  frameNumber(0U),
  storageNumber(0U),
  fileSize(0U),
  transferState(TransferState::IDLE),
  otaTimeoutTimer(0U),
  fileNamePtr(nullptr),
  imageCrc(0U),
  imageCrcKnown(false),
  sharedImage(nullptr) {}

CanOta::~CanOta() {
  if(receivedFile) {
    receivedFile.close();
  }
}

CanOta::OtaStartErrorType CanOta::startOta(const char* fileName, uint16_t storageNumber, OtaImageInfo* image) {
  ErrorState<OtaStartError, OtaStartErrorType> otaStartErrState;
  if(isOtaInProgress()) {
    otaStartErrState.setError(OtaStartError::ALREADY_IN_PROGRESS);
    return otaStartErrState.getRawErrorState();
  }
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
    transferState = TransferState::INVALID;
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
  sharedImage = image;
  imageCrcKnown = (image != nullptr) && image->valid && (image->size == fileSize);
  imageCrc = imageCrcKnown ? image->crc : 0U;
  otaTimeoutTimer = millis();
  return otaStartErrState.getRawErrorState();
}

void CanOta::handleOtaCanFrames(const CanHandler::CanFrame& canFrame) { // NOLINT(readability-convert-member-functions-to-static)
  if(transferState != TransferState::WAIT_FOR_ACK) { return; }
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

void CanOta::checksumOrSendStart() {
  if(!imageCrcKnown) {
    const uint32_t remainingBytes = receivedFile.available();
    if(remainingBytes > 0U) {
      uint8_t readBuffer[readBufferSize] = { 0U };
      const uint8_t readLength = (remainingBytes >= readBufferSize) ? readBufferSize : remainingBytes;
      if(!readFilePiece(readBuffer, readLength)) { return; }
      crc16.next(readBuffer, readLength);
      return;
    }
    imageCrc = crc16.get();
    imageCrcKnown = true;
    if(sharedImage != nullptr) {
      sharedImage->size = fileSize;
      sharedImage->crc = imageCrc;
      sharedImage->valid = true;
    }
  }
  receivedFile.seek(0U, SeekSet);
  OtaCanFrame::StartFrame startFrame;
  startFrame.storageNumber = storageNumber;
  startFrame.fwSize = fileSize;
  startFrame.fwCrc = imageCrc;
  uint8_t canData[8] = { 0U };
  OtaCanFrame::packStart(startFrame, canData);
  transferState = canMqttGateway.sendCanFrame(CanCmd::OTA_START, canData) ? TransferState::WAIT_FOR_ACK : TransferState::INVALID;
}

void CanOta::sendNextPiece() {
  const uint32_t remainingFileSize = receivedFile.available();
  if(remainingFileSize == 0U) {
    transferState = TransferState::WAIT_FOR_ACK;
    return;
  }
  const uint8_t bytesNumber = (remainingFileSize >= filePieceSize) ? filePieceSize : remainingFileSize;
  OtaCanFrame::SendFrame sendFrame;
  if(!readFilePiece(sendFrame.data, bytesNumber)) { return; }
  sendFrame.dataAddress = frameNumber;
  uint8_t canData[8] = { 0U };
  OtaCanFrame::packSend(sendFrame, canData);
  frameNumber += bytesNumber;
  transferState = canMqttGateway.sendCanFrame(CanCmd::OTA_SEND, canData) ? TransferState::WAIT_FOR_ACK : TransferState::INVALID;
}

bool CanOta::readFilePiece(uint8_t* buffer, uint8_t length) {
  if(receivedFile.read(buffer, length) == length) { return true; }
  Logger::get()->printf_P(PSTR("[CAN] Short read from \"%s\"\r\n"), fileNamePtr);
  transferState = TransferState::INVALID;
  return false;
}

void CanOta::runOta() {
  const uint32_t actualTime = millis();
  if(Time::hasElapsed(actualTime, otaTimeoutTimer, otaTimeoutTime)) {
    // Route an in-progress timeout through INVALID so the existing cleanup runs: the file gets
    // closed, the counters reset, and the {"OTA":"[ERR]"} status reaches the server.
    // VALID/INVALID are excluded: they are processed within the same pass, and a timeout must
    // not overwrite an already-arrived final ACK.
    const bool otaInProgress = (transferState == TransferState::START) || (transferState == TransferState::STORE) || (transferState == TransferState::WAIT_FOR_ACK);
    if(otaInProgress) {
      Logger::get()->printf_P(PSTR("[CAN] OTA timeout for \"%s\"!\r\n"), canMqttGateway.getSubtopic());
      transferState = TransferState::INVALID;
    }
  }

  switch(transferState) {
    case TransferState::IDLE: {
      otaTimeoutTimer = actualTime;
    } break;
    case TransferState::WAIT_FOR_ACK: {
    } break;
    case TransferState::START: {
      checksumOrSendStart();
    } break;
    case TransferState::STORE: {
      otaTimeoutTimer = actualTime;
      sendNextPiece();
    } break;
    case TransferState::VALID:
    case TransferState::INVALID: {
      {
        const bool otaStatus = (transferState == TransferState::VALID);
        Logger::get()->printf_P(PSTR("[CAN] File transfer to \"%s\": %s\r\n"), canMqttGateway.getSubtopic(), Str::getStateStr(otaStatus));
        char dataOut[otaFrameBufSize] = { '\0' };
        const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), otaFrame, Str::getStateStr(otaStatus));
        const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
        if(dataOutValid) {
          (void)canMqttGateway.sendOtaStatusMessage(dataOut);
        }
      }
      if(receivedFile) {
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
  clientEverSeen(false),
  fwFileNamePtr(fwFileName),
  batchImage() {
  if(fwFileNamePtr != nullptr) {
    OtaRegistry::add(*this);
  }
}

bool CanMqttGateway::startOta(const char* fileName, OtaImageInfo& image) {
  return startOta(fileName, &image);
}

bool CanMqttGateway::startOta(const char* fileName) { // NOLINT(readability-convert-member-functions-to-static)
  return startOta(fileName, nullptr);
}

bool CanMqttGateway::startOta(const char* fileName, OtaImageInfo* image) { // NOLINT(readability-convert-member-functions-to-static)
  const uint8_t otaStartResultCode = canOta.startOta(fileName, 0U, image);
  const bool fileTransferStartResult = (otaStartResultCode == 0U);
  Logger::get()->printf_P(PSTR("[CAN] File transfer starts to \"%s\": %s\r\n"),
                          MqttBase::getSubtopic(), Str::getStateStr(fileTransferStartResult));
  if(!fileTransferStartResult) {
    Logger::get()->printf_P(Str::getErrCodeFmt(), otaStartResultCode);
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

bool CanMqttGateway::requestCanIdChange(uint16_t newLocalCanId) { // NOLINT(readability-convert-member-functions-to-static)
  // The address this device already answers on counts as free: being told to keep it is a valid
  // request, and the node accepts it too. A master that lost an answer can repeat itself.
  const bool addressIsAlreadyMine = (newLocalCanId == getClientCanId());
  if(!CanIdAssign::isAssignableId(newLocalCanId) || (!addressIsAlreadyMine && !isClientCanIdFree(newLocalCanId))) {
    Logger::get()->printf_P(PSTR("[CAN] Refusing address %hu for \"%s\"\r\n"), newLocalCanId, MqttBase::getSubtopic());
    return false;
  }
  CanIdAssign::Request request;
  request.expectedLocal = getClientCanId();
  request.newLocal = newLocalCanId;
  uint8_t canData[8] = { 0U };
  CanIdAssign::pack(request, canData);
  Logger::get()->printf_P(PSTR("[CAN] Address %hu -> %hu for \"%s\"\r\n"), request.expectedLocal, newLocalCanId, MqttBase::getSubtopic());
  return sendCanFrame(CanCmd::SET_CAN_ID, canData);
}

void CanMqttGateway::buildCanTopics() {
  // These buffers are what a discovery payload is built from, and both this and the payload can
  // be reached from either task once the CAN drivers run on one of their own.
  const LockGuard guard = lockShared();
  if(canTopicsBuilt) { return; }
  const char* sender = MqttBase::getSenderTopicStr();
  const char* sub = MqttBase::getSubtopic();
  const char* client = MqttBase::getClientNameStr();
  if(sender == nullptr || sub == nullptr || client == nullptr) { return; }
  if(sender[0] == '\0' || sub[0] == '\0' || client[0] == '\0') { return; }

  // Build base: senderTopic + subtopic + "/" — used as %s argument for the topic templates.
  char base[MqttTopics::getSenderTopicBufSize() + MqttBase::getSubtopicSize()] = {};
  strlcpy(base, sender, sizeof(base));
  strlcat(base, sub, sizeof(base));
  strlcat(base, "/", sizeof(base));

  (void)snprintf_P(canAvailTopic, sizeof(canAvailTopic), MqttTopics::getMqttAvailTopic(), base);
  (void)snprintf_P(canInfoTopic, sizeof(canInfoTopic), MqttTopics::getMqttInfoTopic(), base);
  (void)snprintf_P(canOtaTopic, sizeof(canOtaTopic), PSTR("%s%s"), base, canOtaSuffix);
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
  (void)MqttBase::sendRetainedSubtopic(availSubtopic, MqttTopics::getAvailOfflinePayload());
  clientOnline = false;
  return initLocal();
}

bool CanMqttGateway::run() {
  handlePing();
  // The turn is taken here rather than handed over by whoever received the file: the transfer
  // this starts is driven from this same run(), so no other task ever touches its state.
  if(!canOta.isOtaInProgress() && OtaRegistry::claimStart(*this, batchImage)) {
    (void)startOta(fwFileNamePtr, batchImage);
  }
  const bool checksumWasKnown = batchImage.valid;
  canOta.runOta();
  // The read pass leaves its result in batchImage; the rest of the batch is spared it.
  if(!checksumWasKnown && batchImage.valid) { OtaRegistry::reportImage(batchImage); }
  return runLocal();
}

void CanMqttGateway::handlePing() {
  const uint32_t actualTime = millis();
  if(Time::hasElapsed(actualTime, clientPingTimer, clientPingTime)) {
    (void)sendCanFrame(CanCmd::PING);
    clientPingTimer = actualTime;
  }
  const bool clientOnlineActual = clientEverSeen && !Time::hasElapsed(actualTime, clientOfflineTimer, clientOfflineTime);
  if(clientOnline != clientOnlineActual) {
    clientOnline = clientOnlineActual;
    Logger::get()->printf_P(PSTR("[CAN] %s is %s!\r\n"), MqttBase::getSubtopic(),
                            Str::getOnlineStateStr(clientOnline));
    const char* availSubtopic = canAvailTopic + (MqttTopics::getSenderTopicBufSize() - 1U);
    (void)MqttBase::sendRetainedSubtopic(availSubtopic, clientOnline ? MqttTopics::getAvailOnlinePayload() : MqttTopics::getAvailOfflinePayload());
  }
}

void CanMqttGateway::canFrameArrivedCallback(const CanHandler::CanFrame& canFrame) {
  clientPingTimer = clientOfflineTimer = millis();
  clientEverSeen = true;
  switch(static_cast<uint16_t>(canFrame.cmd)) {
    case static_cast<uint16_t>(CanCmd::PING): {
    } break;
    case static_cast<uint16_t>(CanCmd::RESTART): {
      (void)sendCanFrame(CanCmd::FW_VERSION);
      // Publish offline then online so brief restarts are visible in HA connection history,
      // even if the device came back before the ping timeout would have caught the outage.
      // clientOnline is forced true so handlePing() does not publish a duplicate online.
      const char* availSubtopic = canAvailTopic + (MqttTopics::getSenderTopicBufSize() - 1U);
      (void)MqttBase::sendRetainedSubtopic(availSubtopic, MqttTopics::getAvailOfflinePayload());
      (void)MqttBase::sendRetainedSubtopic(availSubtopic, MqttTopics::getAvailOnlinePayload());
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
      const uint8_t resetReason = canFrame.data[7];   // MCUSR bits plus the intentional-restart bit
      {
        const LockGuard guard = lockShared();                       // canSwVersion feeds the discovery payload.
        (void)snprintf(canSwVersion, sizeof(canSwVersion), "%hu (%08x)", fwVersion, gitHash);
      }
      char dataOut[MqttTopics::getInfoPayloadBufSize()] = { '\0' };
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), MqttTopics::getMqttInfoPayload(), fwVersion, gitHash, gitDirty, resetReason,
                                             static_cast<uint8_t>(BootStage::Unknown));   // A CAN device runs no startup of ours to report.
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(dataOutValid) {
        const char* infoSubtopic = canInfoTopic + (MqttTopics::getSenderTopicBufSize() - 1U);
        (void)MqttBase::sendRetainedSubtopic(infoSubtopic, dataOut);
      }
      if(!clientOnline) {
        const char* availSubtopic = canAvailTopic + (MqttTopics::getSenderTopicBufSize() - 1U);
        (void)MqttBase::sendRetainedSubtopic(availSubtopic, MqttTopics::getAvailOnlinePayload());
        clientOnline = true;
      }
      (void)publishDiscovery();
    } break;
    case static_cast<uint16_t>(CanCmd::SET_CAN_ID): {
      // The only confirmation the master gets. The node restarts right after answering, so a
      // refusal is the difference between "it moved" and "it is still where it was".
      const bool accepted = (canFrame.data[0] == static_cast<uint8_t>(CanHandler::Response::ACK));
      Logger::get()->printf_P(PSTR("[CAN] \"%s\" %s the new address\r\n"), MqttBase::getSubtopic(),
                              accepted ? PSTR("took") : PSTR("refused"));
    } break;
    case static_cast<uint16_t>(CanCmd::BUTTON_EVENT): {
      const uint8_t buttonState = canFrame.data[0];
      char dataOut[buttonFrameBufSize] = { '\0' };
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
