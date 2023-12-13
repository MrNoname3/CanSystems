#include "common.hpp"
#include <ArduinoJson.h>                      /// Handle JSON files.
#include <Base64.hpp>

const char Common::COMMON_PREFIX[] PROGMEM              = "[COMMON] ";

Common::Common(const char* classID, Stream* serial) : MqttComBase(classID), serialPort(serial), ota(serial) {}

void Common::restartESP() {
  if(serialPort) { serialPort->printf_P(PSTR("%sRestarting...\r\n"), COMMON_PREFIX); }
  if(serialPort) { serialPort->flush(); }             // Sends out data from serial buffer, before reset.
  ESP.restart();
  delay(10000);                                       // Prevent doing anything before restart.
}

void Common::messageReceived(uint8_t* payload, uint32_t length) {
  StaticJsonDocument<512> cmdJson;
  DeserializationError deserializationError = deserializeJson(cmdJson, payload, length);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(!deSerResult){
    if(serialPort) { serialPort->printf_P(PSTR("%sDeserialisation failed!\r\n"), COMMON_PREFIX); }
  }
  if(deSerResult) {
    const uint8_t cmd = cmdJson["cmd"].as<uint8_t>();
    Command command = static_cast<Command>(cmd);
    switch(command) {
      case Command::BLANK: {} break;
      case Command::RESTART: { restartESP(); } break;
      case Command::OTA_START: {
        const uint32_t fwSize = cmdJson[F("fwSize")].as<uint32_t>();
        const uint32_t fwCrc = cmdJson[F("crc32")].as<uint32_t>();
        const bool otaBeginResult = ota.begin(fwSize, fwCrc);
        if(!otaBeginResult) {
          if(serialPort) { serialPort->printf_P(PSTR("%sCan't begin OTA!\r\n"), COMMON_PREFIX); }
          return;
        }
      } break;
      case Command::OTA_DATA: {
        uint8_t fwData[336];
        const uint32_t fwPieceNumber = cmdJson[F("piece")].as<uint32_t>();
        const char* fwDataB64 = cmdJson["data"].as<const char*>();
        const uint32_t fwDataB64Size = strlen(fwDataB64);
        const uint32_t decodedSize = Base64::decodedLength(reinterpret_cast<const uint8_t*>(fwDataB64), fwDataB64Size);
        if(decodedSize > sizeof(fwData)) {
          if(serialPort) { serialPort->printf_P(PSTR("%sFW piece size error!\r\n"), COMMON_PREFIX); }
          return;
        }
        const uint32_t decodedSize2 = Base64::decodeBase64(reinterpret_cast<const uint8_t*>(fwDataB64), fwData, fwDataB64Size);
        if(decodedSize != decodedSize2) {
          if(serialPort) { serialPort->printf_P(PSTR("%sDecoded size check error!\r\n"), COMMON_PREFIX); }
          return;
        }
        if(!ota.store(fwPieceNumber, fwData, decodedSize)) {
          if(serialPort) { serialPort->printf_P(PSTR("%sFW storing failed!\r\n"), COMMON_PREFIX); }
        }
      } break;
      case Command::OTA_END: {
        if(!ota.checkValidity()) {
          if(serialPort) { serialPort->printf_P(PSTR("%sStored FW is not valid!\r\n"), COMMON_PREFIX); }
        }
      } break;
    };
  }
}