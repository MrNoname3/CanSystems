#include "mqttCommon.hpp"
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.

MqttCommon::MqttCommon(Connectivity& connectivity, const char* classID, HardwareSerial& serial) :
  MqttBase(connectivity, classID),
  serial(serial),
  externalFileName{'\0'},
  dataTransfer(this->serial)
{}

void MqttCommon::messageArrivedCallback(const uint8_t* payload, uint32_t length) {
  JsonDocument cmdJson;
  DeserializationError deserializationError = deserializeJson(cmdJson, payload, length);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(!deSerResult) {
    serial.printf_P(PSTR("[COMMON] Deserialisation failed: %s\r\n"), reinterpret_cast<const char*>(deserializationError.f_str()));
    return;
  }
  const uint8_t cmd = cmdJson[F("cmd")].as<uint8_t>();
  Command command = static_cast<Command>(cmd);
  switch(command) {
    case Command::BLANK: {} break;
    case Command::RESTART: { ResetHandler::restartMCU(); } break;
    case Command::FW_DT_START:
    case Command::WIFICFG_DT_START:
    case Command::EXT_FILE_DT_START: {
      const uint32_t fileSize = cmdJson[F("fileSize")].as<uint32_t>();
      const uint32_t fileCrc = cmdJson[F("crc32")].as<uint32_t>();
      const char* fileNamePtr = nullptr;
      switch(command) {
        case Command::FW_DT_START: {
          const char* binId = cmdJson[F("binId")].as<const char*>();
          if(strncmp_P(binId, Build::getPioEnv(), Build::getPioEnvLength()) != 0) {
            serial.printf_P(PSTR("[COMMON] Wrong FW file ID: %s\r\n"), binId);
            if(!MqttBase::sendResponse(MqttBase::Response::NACK, cmd)) { return; /*Handler needed*/ }
            return;
          }
          fileNamePtr = FileName::getOtaFwLocation();
          } break;
        case Command::WIFICFG_DT_START: { fileNamePtr = FileName::getWifiTempConfigLocation(); } break;
        case Command::EXT_FILE_DT_START: {
          const char* fileName = cmdJson[F("name")].as<const char*>();
          memset(externalFileName, '\0', sizeof(externalFileName));
          if(fileName != nullptr) { memccpy(externalFileName, fileName, '\0', sizeof(externalFileName)); }
          uint32_t externalFileNameSize =  strnlen(externalFileName, sizeof(externalFileName));
          if(externalFileNameSize == 0 || externalFileNameSize >= sizeof(externalFileName)) {
            serial.printf_P(PSTR("[COMMON] Wrong file name: missing / too long!\r\n"));
            if(!MqttBase::sendResponse(MqttBase::Response::NACK, cmd)) { return; /*Handler needed*/ }
            return;
          }
          fileNamePtr = static_cast<const char*>(externalFileName);
        } break;
        default: {} break;
      }
      const bool transferBeginResult = dataTransfer.begin(fileSize, fileCrc, fileNamePtr);
      if(!MqttBase::sendResponse((transferBeginResult ? MqttBase::Response::ACK : MqttBase::Response::NACK), cmd)) { return; /*Handler needed*/ }
      if(!transferBeginResult) {
        serial.printf_P(PSTR("[COMMON] Can't begin file transfer:\r\n  Name: %s\r\n"), fileNamePtr);
        return;
      }
    } break;
    case Command::FW_DT_DATA:
    case Command::WIFICFG_DT_DATA:
    case Command::EXT_FILE_DT_DATA: {
      const uint32_t filePieceNumber = cmdJson[F("piece")].as<uint32_t>();
      const char* filePieceB64 = cmdJson["data"].as<const char*>();
      const bool storingResult = dataTransfer.storeBase64(filePieceNumber, filePieceB64);
      if(!MqttBase::sendResponse(storingResult ? MqttBase::Response::ACK : MqttBase::Response::NACK, cmd)) { return; /*Handler needed*/ }
      if(!storingResult) {
        serial.printf_P(PSTR("[COMMON] File storing failed!\r\n"));
      }
    } break;
    case Command::FW_DT_END:
    case Command::WIFICFG_DT_END:
    case Command::EXT_FILE_DT_END: {
      const bool validityCheckResult = dataTransfer.checkValidity();
      if(!MqttBase::sendResponse((validityCheckResult ? MqttBase::Response::ACK : MqttBase::Response::NACK), cmd)) { return; /*Handler needed*/ }
      if(!validityCheckResult) {
        serial.printf_P(PSTR("[COMMON] Stored file is not valid!\r\n"));
        return;
      }
      if(command == Command::FW_DT_END) {
        const bool fwUpdatePreparationOk = dataTransfer.upgradeFirmware();
        if(!fwUpdatePreparationOk) {
          serial.printf_P(PSTR("[COMMON] FW upgrade preparation failed!\r\n"));
          return;
        }
        ResetHandler::restartMCU();
      }
    } break;
  };
}

bool MqttCommon::init() {
  char versionString[80] = {'\0'};
  const int32_t versionStringSize = snprintf_P(versionString, sizeof(versionString),
    PSTR("{""\"CPP\":%u,\"FW\":%hu,\"GH\":\"%x\",\"Dirty\":%hu,\"RR\":%hu""}"),
    Build::getCppVersion(), Build::getFwVersion(), Build::getGitHash(), Build::getGitDirty(), ResetHandler::getResetReason());
  const bool versionStringValid = (versionStringSize >= 0 && versionStringSize < static_cast<int32_t>(sizeof(versionString)));
  if(!versionStringValid) { return false; }
  if(!MqttBase::sendMessage(versionString)) { return false; }
  return true;
}

bool MqttCommon::run() { return true; }