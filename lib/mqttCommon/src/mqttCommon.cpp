#include "mqttCommon.hpp"
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include "common.hpp"                                               /// Common definitions and functions.

MqttCommon::MqttCommon(Connectivity& connectivity, const char* subtopic) :
  MqttBase(connectivity, subtopic),
  dataTransfer(fileValidCb)
{}

bool MqttCommon::init() { // NOLINT(readability-convert-member-functions-to-static)
  return true;
}

bool MqttCommon::publishDiscovery() { // NOLINT(readability-convert-member-functions-to-static)
  using HA = Connectivity::HADiscovery;
  const HA::EntityConfig config = HA::EntityConfig::button(PSTR("Reboot"), cmdReboot, HA::DeviceClass::restart);
  return doPublishEntityDiscovery(config);
}

bool MqttCommon::run() {
  if(isFileCheckDone) {
    isFileCheckDone = false;
    const uint32_t errCode = dataTransfer.getErrorCode();
    sendResponse(isFileValid, errCode);
    if(!isFileValid) {
      Logger::get().printf_P(PSTR("[COMMON] Stored file is not valid!\r\n  Code: %u\r\n"), errCode);
    } else {
      if(isRestartRequired) {
        reboot();
      } else {
        OtaRegistry::triggerForFile(dataTransfer.getFileName());
      }
    }
  }
  dataTransfer.runValidityCheck();
  return true;
}

void MqttCommon::fileValidCb(bool isValid) {
  isFileCheckDone = true;
  isFileValid = isValid;
}

bool MqttCommon::sendResponse(bool result, uint32_t errCode) { // NOLINT(readability-convert-member-functions-to-static)
  const bool sendingResult = MqttBase::sendResponse((result ? MqttBase::Response::ACK : MqttBase::Response::NACK), 0U, errCode);
  if(!sendingResult) {
    Logger::get().printf_P(PSTR("[COMMON] Failed to send response '%hu'\r\n"), static_cast<uint8_t>(result));
  }
  return sendingResult;
}

void MqttCommon::messageArrivedCallback(JsonDocument& payloadJson) {
  JsonVariant binIdJsonVar = payloadJson[F("binId")];
  JsonVariant fileNameJsonVar = payloadJson[F("name")];
  JsonVariant fileSizeJsonVar = payloadJson[F("fileSize")];
  JsonVariant fileMd5JsonVar = payloadJson[F("md5")];
  JsonVariant filePieceJsonVar = payloadJson[F("piece")];
  JsonVariant fileDataJsonVar = payloadJson[F("data")];
  JsonVariant cmdJsonVar = payloadJson[F("cmd")];

  // Check for a command message first, before any file transfer fields.
  if(cmdJsonVar.is<const char*>()) {
    dispatchCommand(cmdJsonVar.as<const char*>());
    return;
  }

  const bool binIdPresented = binIdJsonVar.is<const char*>();
  const bool fileNamePresented = fileNameJsonVar.is<const char*>();
  const bool fileSizePresented = fileSizeJsonVar.is<uint32_t>();
  const bool fileMd5Presented = fileMd5JsonVar.is<const char*>();
  const bool filePiecePresented = filePieceJsonVar.is<uint32_t>();
  const bool fileDataPresented = fileDataJsonVar.is<const char*>();

  if(fileNamePresented && fileSizePresented && fileMd5Presented) {
    if(binIdPresented) {
      const char* binId = binIdJsonVar.as<const char*>();
      if(strncmp_P(binId, Build::getPioEnv(), Build::getPioEnvLength()) != 0) {
        Logger::get().printf_P(PSTR("[COMMON] Wrong FW file ID: '%s' expected: '%s'\r\n"), binId, Build::getPioEnv());
        sendResponse(false);
        return;
      }
      isRestartRequired = true;
    } else {
      isRestartRequired = false;
    }
    const uint32_t fileSize = fileSizeJsonVar.as<uint32_t>();
    const char* fileMd5 = fileMd5JsonVar.as<const char*>();
    const char* fileName = fileNameJsonVar.as<const char*>();
    const bool transferBeginResult = dataTransfer.begin(fileSize, fileMd5, fileName);
    const uint32_t beginErrCode = dataTransfer.getErrorCode();
    sendResponse(transferBeginResult, beginErrCode);
    if(!transferBeginResult) {
      Logger::get().printf_P(PSTR("[COMMON] Can't begin file transfer: %s\r\n  Code: %u\r\n"), fileName, beginErrCode);
    }
  } else if(filePiecePresented && fileDataPresented) {
    const uint32_t filePieceNumber = filePieceJsonVar.as<uint32_t>();
    const char* filePieceB64 = fileDataJsonVar.as<const char*>();
    const bool storingResult = dataTransfer.storeBase64(filePieceNumber, filePieceB64);
    const uint32_t storingErrCode = dataTransfer.getErrorCode();
    sendResponse(storingResult, storingErrCode);
    if(!storingResult) {
      Logger::get().printf_P(PSTR("[COMMON] File storing failed!\r\n  Code: %u\r\n"), storingErrCode);
    }
  } else {
    Logger::get().printf_P(PSTR("[COMMON] Unknown JSON file!\r\n"));
  }
}

void MqttCommon::reboot() { // NOLINT(readability-convert-member-functions-to-static)
  shutdownMqtt();
  ResetHandler::restartMCU();
}

// Lookup table mapping command strings to their handler functions.
const MqttCommon::CmdEntry MqttCommon::cmdTable[] = {
  { cmdReboot, &MqttCommon::handleReboot },
};

void MqttCommon::dispatchCommand(const char* cmd) {
  for(const CmdEntry& entry : cmdTable) {
    // cppcheck-suppress useStlAlgorithm
    if(strncmp_P(cmd, entry.name, maxCmdLength) == 0) {
      (this->*entry.handler)();
      return;
    }
  }
  Logger::get().printf_P(PSTR("[COMMON] Unknown cmd: '%s'\r\n"), cmd);
  sendResponse(false);
}

void MqttCommon::handleReboot() {
  Logger::get().printf_P(PSTR("[COMMON] Reboot command received.\r\n"));
  sendResponse(true);
  reboot();
}
