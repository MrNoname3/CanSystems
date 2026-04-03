#include "mqttCommon.hpp"
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include "common.hpp"                                               /// Common definitions and functions.

MqttCommon::MqttCommon(Connectivity& connectivity, const char* subtopic) :
MqttBase(connectivity, subtopic),
dataTransfer(fileValidCb),
isRestartRequired(false)
{}

bool MqttCommon::init() { // NOLINT(readability-convert-member-functions-to-static)
  char versionString[dataOutBufSize] = {'\0'};
  const int32_t versionStringSize = snprintf_P(versionString, sizeof(versionString), versionMessageFrame,
  Build::getCppVersion(), Build::getFwVersion(), Build::getGitHash(), Build::getGitDirty(), ResetHandler::getResetReason());
  const bool versionStringValid = (versionStringSize >= 0 && versionStringSize < static_cast<int32_t>(sizeof(versionString)));
  if(!versionStringValid) { return false; }
  if(!MqttBase::sendMessage(versionString)) { return false; }
  return true;
}

bool MqttCommon::run() {
  if(isFileCheckDone) {
    isFileCheckDone = false;
    sendResponse(isFileValid);
    if(!isFileValid) {
      Logger::get().printf_P(PSTR("[COMMON] Stored file is not valid!\r\n  Code: %u\r\n"), dataTransfer.getErrorCode());
    } else {
      if(isRestartRequired) {
        ResetHandler::restartMCU();
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

bool MqttCommon::sendResponse(bool result) { // NOLINT(readability-convert-member-functions-to-static)
  const bool sendingResult = MqttBase::sendResponse((result ? MqttBase::Response::ACK : MqttBase::Response::NACK), 0U);
  if(!sendingResult) {
    Logger::get().printf_P(PSTR("[COMMON] Failed to send respons '%hu'\r\n"), static_cast<uint8_t>(result));
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
  
  const char* fileNamePtr = nullptr;
  if(binIdPresented && fileSizePresented && fileMd5Presented) {
    const char* binId = binIdJsonVar.as<const char*>();
    if(strncmp_P(binId, Build::getPioEnv(), Build::getPioEnvLength()) != 0) {
      Logger::get().printf_P(PSTR("[COMMON] Wrong FW file ID: '%s' expected: '%s'\r\n"), binId, Build::getPioEnv());
      sendResponse(false);
      return;
    }
    isRestartRequired = true;
    fileNamePtr = FileName::getOtaFwLocation();
  } else if(fileNamePresented && fileSizePresented && fileMd5Presented) {
    isRestartRequired = false;
    fileNamePtr = fileNameJsonVar.as<const char*>();
  } else if(filePiecePresented && fileDataPresented) {
    const uint32_t filePieceNumber = filePieceJsonVar.as<uint32_t>();
    const char* filePieceB64 = fileDataJsonVar.as<const char*>();
    const bool storingResult = dataTransfer.storeBase64(filePieceNumber, filePieceB64);
    sendResponse(storingResult);
    if(!storingResult) {
      Logger::get().printf_P(PSTR("[COMMON] File storing failed!\r\n  Code: %u\r\n"), dataTransfer.getErrorCode());
      return;
    }
  } else {
    Logger::get().printf_P(PSTR("[COMMON] Unknown JSON file!\r\n"));
    return;
  }
  
  if(fileNamePtr != nullptr) {
    const uint32_t fileSize = fileSizeJsonVar.as<uint32_t>();
    const char* fileMd5 = fileMd5JsonVar.as<const char*>();
    const bool transferBeginResult = dataTransfer.begin(fileSize, fileMd5, fileNamePtr);
    sendResponse(transferBeginResult);
    if(!transferBeginResult) {
      Logger::get().printf_P(PSTR("[COMMON] Can't begin file transfer: %s\r\n  Code: %u\r\n"), fileNamePtr, dataTransfer.getErrorCode());
      return;
    }
  }
}

// Lookup table mapping command strings to their handler functions.
const MqttCommon::CmdEntry MqttCommon::cmdTable[] = {
  { cmdReboot, &MqttCommon::handleReboot },
};

void MqttCommon::dispatchCommand(const char* cmd) {
  for(const auto& entry : cmdTable) {
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
  ResetHandler::restartMCU();
}