#ifdef ESP32
#include "canHandlerEsp32.hpp"
#include <CAN.h>                                                    /// CAN controller library.
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include <cstdlib>                                                  /// Standard library for memory and utilities.
#include "common.hpp"                                               /// Common definitions and functions.

QueueHandle_t CanHandlerEsp32::canRxQueue = nullptr;

CanHandlerEsp32::CanHandlerEsp32(HardwareSerial& serial) :
  serialPort(serial),
  canTxQueue(nullptr),
  canDevicesList{},
  canDevicesListMutex(nullptr)
{}

bool CanHandlerEsp32::init(uint32_t canBaud) {
#if defined(NEW_CAN_ADDRESS) && defined(MASTER_CAN_ADDRESS)
  // Save new CAN IDs.
  static constexpr uint16_t newMasterCanId = static_cast<uint16_t>(MASTER_CAN_ADDRESS);
  static constexpr uint16_t newLocalCanId = static_cast<uint16_t>(NEW_CAN_ADDRESS);
  serialPort.printf_P(PSTR("[CAN] Saving new IDs:%s\r\n  Master: %hu\r\n  Local: %hu\r\n"),
    Str::getStateStr(canBeginResult), newMasterCanId, newLocalCanId);
#endif
  { // Load CAN ID's.
  const bool canIdLoadingResult = loadCanIds();
  serialPort.printf_P(PSTR("[CAN] Loading IDs:%s\r\n  Master: %hu\r\n  Local: %hu\r\n"),
    Str::getStateStr(canIdLoadingResult), getMasterCanId(), getLocalCanId());
  if(!canIdLoadingResult) { return false; }
  }
  { // Initialise CAN peripheral.
    const bool canBeginResult = CAN.begin(canBaud) == 1;
    serialPort.printf_P(PSTR("[CAN] Init:%s\r\n"), Str::getStateStr(canBeginResult));
    CAN.onReceive(rxInterrupt);
    if(!canBeginResult) { return false; }
  }
  { // Set up the CAN filtering.
    const bool setFilterResult = CAN.filterExtended(
      CanHandlerBase::getCanFilteredId(), CanHandlerBase::getCanIdFilterMask()) == 1;
    serialPort.printf_P(PSTR("[CAN] Set up filter:%s\r\n"), Str::getStateStr(setFilterResult));
    if(!setFilterResult) { return false; }
  }
  { // Setup message queues.
    canRxQueue = xQueueCreate(canRxQueueSize, sizeof(CanFrame));  // Create FIFO queue for RX CAN packets.
    canTxQueue = xQueueCreate(canTxQueueSize, sizeof(CanFrame));
    //configASSERT(canRxQueue != nullptr);                          // Assert if the queue creation fails.
    //configASSERT(canTxQueue != nullptr);
    const bool rxQueueResult = (canRxQueue != nullptr);           // Check queue creation.
    const bool txQueueResult = (canTxQueue != nullptr);
    serialPort.printf_P(PSTR("[CAN] Creating queues:\r\n  RX -> %s\r\n  TX -> %s\r\n"),
      Str::getStateStr(rxQueueResult), Str::getStateStr(txQueueResult));
    if(!rxQueueResult || !txQueueResult) { return false; }
  }
  { // Setup mutex.
    canDevicesListMutex = xSemaphoreCreateMutex();
    if(canDevicesListMutex == nullptr) {
      serialPort.printf_P(PSTR("[CAN] Mutex setup failed!\r\n"));
      return false;
    }
  }
  return true;
}

bool CanHandlerEsp32::send(uint16_t command, const uint8_t (&data)[8]) const {
  if(isDeviceMaster()) { return false; }
  return send(CanFrame{getMasterCanId(), command, getLocalCanId(), data});
}

bool CanHandlerEsp32::send(const CanFrame& frameOut) const {
  return (xQueueSend(canTxQueue, &frameOut, canTxQueueTimeout) == pdTRUE);
}

void CanHandlerEsp32::rxInterrupt(int packetsNum) {
  if(packetsNum <= 0) { return; }
  CanFrame rxCanData;
  rxCanData.extId = CAN.packetId();
  if(!CAN.packetRtr()) {
    const uint8_t canDataDlc = static_cast<uint8_t>(CAN.packetDlc());
    const uint8_t bytesReaded = static_cast<uint8_t>(CAN.readBytes(rxCanData.data, canDataDlc));
    if(canDataDlc != bytesReaded) { return; }
  }
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if(xQueueSendFromISR(canRxQueue, &rxCanData, &xHigherPriorityTaskWoken) != pdTRUE) {
    return;
  }
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

bool CanHandlerEsp32::run() {
  { // Handle received CAN frame.
    CanFrame frameIn;
    if(xQueueReceive(canRxQueue, &frameIn, static_cast<TickType_t>(0U)) == pdTRUE) {
      const uint16_t nodeCanId = static_cast<uint16_t>(frameIn.from);
      if(xSemaphoreTake(canDevicesListMutex, semaphoreTimeout) == pdTRUE) {
        for(const auto &currentcanDevice : canDevicesList) {
          if(currentcanDevice == nullptr) { continue; }
          if(currentcanDevice->getClientCanId() == nodeCanId) {
            currentcanDevice->canFrameArrivedCallback(frameIn);
            xSemaphoreGive(canDevicesListMutex);
            break;
          }
        }
        xSemaphoreGive(canDevicesListMutex);
      }
    }
  }
  { // Handle CAN frame sending.
    CanFrame frameOut;
    if(xQueueReceive(canTxQueue, &frameOut, static_cast<TickType_t>(0U)) == pdTRUE) {
      const bool beginPacketResult = CAN.beginExtendedPacket(frameOut.extId) > 0;
      if(!beginPacketResult) { return false; }
      const bool packetWriteResult = CAN.write(frameOut.data, sizeof(frameOut.data)) > 0U;
      if(!packetWriteResult) { return false; }
      const bool endPacketResult = CAN.endPacket() > 0;
      if(!endPacketResult) { return false; }
    } else {
      return false;
    }
  }
  return true;
}

bool CanHandlerEsp32::registerCallback(CanBase* canBasePtr) {
  if(canBasePtr == nullptr) { return false; }
  if(xSemaphoreTake(canDevicesListMutex, semaphoreTimeout) == pdTRUE) {
    canDevicesList.push_back(canBasePtr);
    xSemaphoreGive(canDevicesListMutex);
    return true;
  }
  return false;
}

// SoftwareTimer::SoftwareTimer(uint32_t time) : time_(time), start_time_(0) {}

// bool SoftwareTimer::isExpired() { return (millis() - start_time_ >= time_); }

// void SoftwareTimer::reload() { start_time_ = millis(); }

// //////////////////// -- CanComBase class-- ////////////////////

// const char CanComBase::STATUS_ONLINE[] PROGMEM              = "ONLINE";
// const char CanComBase::STATUS_OFFLINE[] PROGMEM             = "OFFLINE";
// const char CanComBase::STATUS_RESTARTED[] PROGMEM           = "RESTARTED";
// const char CanComBase::STATUS_FRAME[] PROGMEM = {
//   "{"
//     "\"Status\":\"%s\""
//   "}"
// };
// const char CanComBase::BUTTON_FRAME[] PROGMEM = {
//   "{"
//     "\"Button\":%hu"
//   "}"
// };
// const char CanComBase::FW_VERSION_FRAME[] PROGMEM = {
//   "{"
//     "\"Firmware\":%hu,"
//     "\"GitHash\":\"%x\""
//   "}"
// };
// const char CanComBase::OTA_FRAME[] PROGMEM = {
//   "{"
//     "\"OTA\":\"%s\""
//   "}"
// };

// CanComBase::CanComBase(CanHandler& canHandler, uint32_t canId, Connectivity& connectivity, const char* classID) :
//   MqttBase(connectivity, classID),
//   canHandler(canHandler),
//   nodeCanId(canId),
//   pingTimer(pingTime),
//   alertTimer(alertTime),
//   nodeAlive_(true),
//   receivedFile(),
//   frameNumber(0),
//   storageNumber(0),
//   transferState(TransferState::IDLE),
//   otaTimeoutTimer(otaTimeoutTime)
// {
//   canHandler.registerCallback(this);
// }

// bool CanComBase::beginPriv() {
//   sendCanCmd(CanCmd::PING);
//   pingTimer.reload();
//   alertTimer.reload();
//   return init();
// }

// bool CanComBase::loopPriv() {
//   if(pingTimer.isExpired()) {
//     pingTimer.reload();
//     sendCanCmd(CanCmd::PING);
//   }
//   const bool nodeAlive = !alertTimer.isExpired();
//   if(nodeAlive != nodeAlive_) {
//     nodeAlive_ = nodeAlive;
//     const char* statusStr = nodeAlive_ ? STATUS_ONLINE : STATUS_OFFLINE;
//     // serialPort.printf_P(PSTR("[CANB] %s is %s!\r\n"), MqttBase::getSubtopic(), statusStr);
//     static constexpr const uint8_t dataOutBufSize = 64;
//     char dataOut[dataOutBufSize] = { '\0' };
//     const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), STATUS_FRAME, statusStr);
//     const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
//     if(!dataOutValid) { return false; }
//     if(!MqttBase::sendMessage(dataOut)) { return false; /*Handler needed*/ }
//   }
//   runOta();
//   return run();
// }

// void CanComBase::canFrameReceivedPriv(CanHandler::CanFrame& canFrame) {
//   pingTimer.reload();
//   alertTimer.reload();
//   const uint16_t command = static_cast<uint16_t>(canFrame.cmd);
//   switch(command) {
//     case static_cast<uint16_t>(CanCmd::PING): {} break;
//     case static_cast<uint16_t>(CanCmd::RESTART): {
//       static constexpr const uint8_t dataOutBufSize = 64;
//       char dataOut[dataOutBufSize] = { '\0' };
//       const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), STATUS_FRAME, STATUS_RESTARTED);
//       const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
//       if(!dataOutValid) { return; }
//       if(!MqttBase::sendMessage(dataOut)) { return; /*Handler needed*/ }
//     } break;
//     case static_cast<uint16_t>(CanCmd::FW_VERSION): {
//       const uint16_t fwVersion =
//         (static_cast<uint16_t>(canFrame.data[0]) << 0) |
//         (static_cast<uint16_t>(canFrame.data[1]) << 8);
//       const uint32_t gitHash = 
//         (static_cast<uint32_t>(canFrame.data[2]) << 0) |
//         (static_cast<uint16_t>(canFrame.data[3]) << 8) |
//         (static_cast<uint16_t>(canFrame.data[4]) << 16) |
//         (static_cast<uint16_t>(canFrame.data[5]) << 24);
//       static constexpr const uint8_t dataOutBufSize = 96;
//       char dataOut[dataOutBufSize] = { '\0' };
//       const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), FW_VERSION_FRAME, fwVersion, gitHash);
//       const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
//       if(!dataOutValid) { return; }
//       if(!MqttBase::sendMessage(dataOut)) { return; /*Handler needed*/ }
//     } break;
//     case static_cast<uint16_t>(CanCmd::BUTTON_EVENT): {
//       const uint8_t buttonState = canFrame.data[0];
//       static constexpr const uint8_t dataOutBufSize = 64;
//       char dataOut[dataOutBufSize] = { '\0' };
//       const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), BUTTON_FRAME, buttonState);
//       const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
//       if(!dataOutValid) { return; }
//       if(!MqttBase::sendMessage(dataOut)) { return; /*Handler needed*/ }
//     } break;
//     case static_cast<uint16_t>(CanCmd::OTA_START):
//     case static_cast<uint16_t>(CanCmd::OTA_SEND): {
//       const Response response = static_cast<Response>(canFrame.data[0]);
//       transferState = (response == Response::ACK) ? TransferState::STORE : TransferState::INVALID;
//     } break;
//     case static_cast<uint16_t>(CanCmd::OTA_END): {
//       const Response response = static_cast<Response>(canFrame.data[0]);
//       transferState = (response == Response::ACK) ? TransferState::VALID : TransferState::INVALID;
//     } break;
//     default: { canFrameReceived(canFrame); } break;
//   }
// }

// void CanComBase::messageArrivedCallback(const uint8_t* payload, uint32_t length) {
//   JsonDocument cmdJson;
//   DeserializationError deserializationError = deserializeJson(cmdJson, payload, length);
//   const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
//   if(!deSerResult) {
//     // serialPort.printf_P(PSTR("[CANB] Deserialisation failed at %s: %s\r\n"),
//     //   MqttBase::getSubtopic(), reinterpret_cast<const char*>(deserializationError.f_str()));
//     return;
//   }
//   JsonVariant fileJsonVar = cmdJson[F("File")];
//   if(fileJsonVar.is<const char*>()) {
//     const char* fileName = fileJsonVar.as<const char*>();
//     const bool fileTransferStartResult = startOta(fileName);
//     // serialPort.printf_P(PSTR("[CANB] File transfer starts to \"%s\":%s\r\n"),
//     //   MqttBase::getSubtopic(), Str::getStateStr(fileTransferStartResult));
//     if(!fileTransferStartResult) { transferState = TransferState::INVALID; }
//     return;
//   }
//   JsonVariant commandJsonVar = cmdJson[F("Command")];
//   JsonVariant dataJsonVar = cmdJson[F("Data")];
//   if(commandJsonVar.is<uint16_t>() && dataJsonVar.is<const char*>()) {
//     const uint16_t command = commandJsonVar.as<uint16_t>();
//     const char* canDataStr = dataJsonVar.as<const char*>();
//     if(canDataStr == nullptr) { return; }
//     char* endPtr = nullptr;
//     const uint64_t canData64 = std::strtoull(canDataStr, &endPtr, 16);
//     if(*endPtr != '\0') { return; }
//     uint8_t canData[8] = { 0 };
//     memcpy(canData, &canData64, sizeof(canData));
//     sendCanFrame(command, canData);
//     return;
//   }
// }

// uint32_t CanComBase::getCanId() const { return nodeCanId; }

// bool CanComBase::sendCanFrame(CanCmd command, const uint8_t (&data)[8]) const {
//   return sendCanFrame(static_cast<uint16_t>(command), data);
// }

// bool CanComBase::sendCanFrame(uint16_t command, const uint8_t (&data)[8]) const {
//   CanHandler::CanFrame canFrame;
//   // canFrame.from = canHandler.localCanId;
//   canFrame.to = nodeCanId;
//   canFrame.cmd = command;
//   memcpy(canFrame.data, data, sizeof(data));
//   return canHandler.send(canFrame);
// }

// bool CanComBase::sendCanCmd(CanCmd command) const {
//   return sendCanCmd(static_cast<uint16_t>(command));
// }

// bool CanComBase::sendCanCmd(uint16_t command) const {
//   uint8_t data[8] = { 0 };
//   return sendCanFrame(command, data);
// }

// bool CanComBase::startOta(const char* fileName) {
//   if(fileName == nullptr) { return false; }
//   const uint32_t fileNameLength = strnlen(fileName, sizeof(this->fileName));
//   if(fileNameLength == 0 || fileNameLength >= sizeof(this->fileName)) { return false; }
//   if(fileName[0] != '/') { return false; }
//   memccpy(this->fileName, fileName, '\0', sizeof(this->fileName));
//   if(!LittleFS.exists(this->fileName)) { return false; }
//   if(receivedFile.name() != nullptr) { receivedFile.close(); }
//   receivedFile = LittleFS.open(FPSTR(this->fileName), "r");
//   if(receivedFile.name() == nullptr) {
//     receivedFile.close();
//     return false;
//   }
//   fileSize = receivedFile.size();
//   if(fileSize == 0) { return false; }
//   crc16.reset();
//   frameNumber = 0;
//   transferState = TransferState::START;
//   return true;
// }

// void CanComBase::runOta() {
//   if(otaTimeoutTimer.isExpired()) {
//     transferState = TransferState::IDLE;
//   }
//   switch(transferState) {
//     case TransferState::IDLE: { otaTimeoutTimer.reload(); } break;
//     case TransferState::START: {
//       const uint32_t remainingBytes = receivedFile.available();
//       if(remainingBytes > 0U) {
//         constexpr uint8_t bufferSize = 64U;
//         uint8_t readBuffer[bufferSize] = { 0 };
//         const uint8_t readLength = remainingBytes >= bufferSize ? bufferSize : remainingBytes;
//         receivedFile.read(readBuffer, readLength);
//         crc16.next(readBuffer, readLength);
//       }
//       else {
//         receivedFile.seek(0, SeekSet);
//         fileCrc = crc16.get();
//         const uint8_t canData[8] = {
//           static_cast<uint8_t>((storageNumber >> 0) & 0xFF),  // Lower byte.
//           static_cast<uint8_t>((storageNumber >> 8) & 0xFF),  // Upper byte.
//           static_cast<uint8_t>((fileSize >> 0) & 0xFF),       // Lowest byte.
//           static_cast<uint8_t>((fileSize >> 8) & 0xFF),
//           static_cast<uint8_t>((fileSize >> 16) & 0xFF),
//           static_cast<uint8_t>((fileSize >> 24) & 0xFF),      // Highest byte.
//           static_cast<uint8_t>((fileCrc >> 0) & 0xFF),        // Lower byte.
//           static_cast<uint8_t>((fileCrc >> 8) & 0xFF),        // Upper byte.
//         };
//         const bool sendResult = sendCanFrame(CanCmd::OTA_START, canData);
//         transferState = sendResult ? TransferState::START_ACK : TransferState::INVALID;
//       }
//     } break;
//     case TransferState::START_ACK: {} break;
//     case TransferState::STORE: {
//       otaTimeoutTimer.reload();
//       constexpr uint8_t pieceSize = 4U;
//       const uint32_t remainingFileSize = receivedFile.available();
//       if(remainingFileSize == 0U) {
//         transferState = TransferState::END_ACK;
//         break;
//       }
//       const uint8_t bytesNumber = remainingFileSize >= pieceSize ? pieceSize : remainingFileSize;
//       uint8_t canData[8] = { 0 };
//       receivedFile.read(canData, bytesNumber);
//       canData[4] = static_cast<uint8_t>((frameNumber >> 0) & 0xFF);
//       canData[5] = static_cast<uint8_t>((frameNumber >> 8) & 0xFF);
//       canData[6] = static_cast<uint8_t>((frameNumber >> 16) & 0xFF);
//       canData[7] = static_cast<uint8_t>((frameNumber >> 24) & 0xFF);
//       frameNumber += bytesNumber;
//       const bool sendResult = sendCanFrame(CanCmd::OTA_SEND, canData);
//       transferState = sendResult ? TransferState::STORE_ACK : TransferState::INVALID;
//     } break;
//     case TransferState::STORE_ACK: {} break;
//     case TransferState::END_ACK: {} break;
//     case TransferState::VALID:
//     case TransferState::INVALID: {
//       receivedFile.close();
//       frameNumber = 0;
//       storageNumber = 0;
//       fileSize = 0;
//       crc16.reset();
//       memset(fileName, '\0', sizeof(fileName));
//       {
//         const bool otaStatus = (transferState == TransferState::VALID);
//         static constexpr const uint8_t dataOutBufSize = 64;
//         char dataOut[dataOutBufSize] = { '\0' };
//         const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), OTA_FRAME, reinterpret_cast<const char*>(otaStatus ? F("OK") : F("ERR")));
//         const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
//         if(dataOutValid) {
//           if(!MqttBase::sendMessage(dataOut)) { return; /*Handler needed*/ }
//         }
//         // serialPort.printf_P(PSTR("[CANB] File transfer for \"%s\":%s\r\n"),
//         //   MqttBase::getSubtopic(), Str::getStateStr(otaStatus));
//       }
//       transferState = TransferState::IDLE;
//     } break;
//   }
// }
#endif // ESP32