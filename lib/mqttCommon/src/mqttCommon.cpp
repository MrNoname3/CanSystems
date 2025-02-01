#include "mqttCommon.hpp"
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include "common.hpp"                                               /// Common definitions and functions.

MqttCommon::MqttCommon(Connectivity& connectivity, const char* subtopic) :
  MqttBase(connectivity, subtopic),
  dataTransfer(fileValidCb),
  isRestartRequired(false)
{}

bool MqttCommon::init() {
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
    sendResponse(isFileValid, Command::FILE_CHECK);
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

bool MqttCommon::sendResponse(bool result, Command command) {
  const bool sendingResult = MqttBase::sendResponse(
    (result ? MqttBase::Response::ACK : MqttBase::Response::NACK), static_cast<uint16_t>(command));
  if(!sendingResult) {
    Logger::get().printf_P(PSTR("[COMMON] Failed to send respons '%hu' for command: '%hu'\r\n"),
      static_cast<uint8_t>(result), static_cast<uint16_t>(command));
  }
  return sendingResult;
}

void MqttCommon::messageArrivedCallback(JsonDocument& payloadJson) {
  JsonVariant cmdJsonVar = payloadJson[F("cmd")];
  if(!cmdJsonVar.is<uint8_t>()) {
    Logger::get().printf_P(PSTR("[COMMON] No 'cmd' key detected in JSON file!\r\n"));
    return;
  }
  const Command command = static_cast<Command>(cmdJsonVar.as<uint8_t>());
  switch(command) {
    case Command::RESTART: {
      ResetHandler::restartMCU();
    } break;
    case Command::FW_DT_START:
    case Command::WIFICFG_DT_START:
    case Command::EXT_FILE_DT_START: {
      const char* fileNamePtr = nullptr;
      isRestartRequired = false;
      if(command == Command::FW_DT_START) {
        JsonVariant binIdJsonVar = payloadJson[F("binId")];
        if(!binIdJsonVar.is<const char*>()) {
          Logger::get().printf_P(PSTR("[COMMON] No 'binId' key detected in JSON file!\r\n"));
          sendResponse(false, command);
          break;
        }
        const char* binId = binIdJsonVar.as<const char*>();
        if(strncmp_P(binId, Build::getPioEnv(), Build::getPioEnvLength()) != 0) {
          Logger::get().printf_P(PSTR("[COMMON] Wrong FW file ID: '%s' expected: '%s'\r\n"), binId, Build::getPioEnv());
          sendResponse(false, command);
          break;
        }
        fileNamePtr = FileName::getOtaFwLocation();
        isRestartRequired = true;
      } else if(command == Command::WIFICFG_DT_START) {
        fileNamePtr = FileName::getWifiConfigLocation();
      } else if(command == Command::EXT_FILE_DT_START) {
        JsonVariant fileNameJsonVar = payloadJson[F("name")];
        if(command == Command::EXT_FILE_DT_START) {
          if(!fileNameJsonVar.is<const char*>()) {
            Logger::get().printf_P(PSTR("[COMMON] No 'name' key detected in JSON file!\r\n"));
            sendResponse(false, command);
            break;
          }
          fileNamePtr = fileNameJsonVar.as<const char*>();
        }
      }

      JsonVariant fileSizeJsonVar = payloadJson[F("fileSize")];
      JsonVariant fileCrc32JsonVar = payloadJson[F("crc32")];
      if(!fileSizeJsonVar.is<uint32_t>() || !fileCrc32JsonVar.is<uint32_t>()) {
        Logger::get().printf_P(PSTR("[COMMON] No 'fileSize' or 'crc32' key detected in JSON file!\r\n"));
        sendResponse(false, command);
        break;
      }
      const uint32_t fileSize = fileSizeJsonVar.as<uint32_t>();
      const uint32_t fileCrc = fileCrc32JsonVar.as<uint32_t>();

      if(fileNamePtr == nullptr) {
        Logger::get().printf_P(PSTR("[COMMON] Wrong file name!\r\n"));
        sendResponse(false, command);
        break;
      }
      const bool transferBeginResult = dataTransfer.begin(fileSize, fileCrc, fileNamePtr);
      sendResponse(transferBeginResult, command);
      if(!transferBeginResult) {
        Logger::get().printf_P(PSTR("[COMMON] Can't begin file transfer: %s\r\n  Code: %u\r\n"), fileNamePtr, dataTransfer.getErrorCode());
        break;
      }
    } break;
    case Command::FILE_PIECE: {
      JsonVariant filePieceJsonVar = payloadJson[F("piece")];
      JsonVariant fileDataJsonVar = payloadJson[F("data")];
      if(!filePieceJsonVar.is<uint32_t>() || !fileDataJsonVar.is<const char*>()) {
        Logger::get().printf_P(PSTR("[COMMON] No 'piece' or 'data' key detected in JSON file!\r\n"));
        sendResponse(false, command);
        break;
      }
      const uint32_t filePieceNumber = filePieceJsonVar.as<uint32_t>();
      const char* filePieceB64 = fileDataJsonVar.as<const char*>();
      const bool storingResult = dataTransfer.storeBase64(filePieceNumber, filePieceB64);
      sendResponse(storingResult, command);
      if(!storingResult) {
        Logger::get().printf_P(PSTR("[COMMON] File storing failed!\r\n  Code: %u\r\n"), dataTransfer.getErrorCode());
        break;
      }
    } break;
    default: {
      Logger::get().printf_P(PSTR("[COMMON] Unknown command: %hu\r\n"), static_cast<uint8_t>(command));
    } break;
  };
}