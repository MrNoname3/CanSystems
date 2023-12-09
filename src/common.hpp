#ifndef COMMON_HPP
#define COMMON_HPP

#include "mqttComBase.hpp"
#include <Arduino.h>                          /// Arduino libraries header.
#include <ArduinoJson.h>                      /// Handle JSON files.
#include "ota.hpp"
#include <Base64.hpp>

class Common : public MqttComBase {
public:
  enum class Command : uint8_t {
    BLANK = 0,
    RESTART,
    OTA_START,
    OTA_DATA,
    OTA_END,
    OTA_STOP
  };

  enum class Response : uint8_t {
    NACK = 0,
    ACK,
  };

  Common(const char* classID, Stream* serial = nullptr) : MqttComBase(classID), serialPort(serial), ota(serial) {}

  /// @brief Destructor of the object.
  virtual ~Common() = default;

  /// @brief Reset the MCU.
  void restartESP() {
    if(serialPort) { serialPort->printf_P(PSTR("%sRestarting...\r\n"), COMMON_PREFIX); }
    if(serialPort) { serialPort->flush(); }             // Sends out data from serial buffer, before reset.
    ESP.restart();
    delay(10000);                                       // Prevent doing anything before restart.
  }

  virtual void messageReceived(uint8_t* payload, uint32_t length) override {
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
          uint8_t fwData[300];
          const uint32_t fwPieceNumber = cmdJson[F("piece")].as<uint32_t>();
          const uint16_t fwDataSize = cmdJson[F("size")].as<uint16_t>();
          const char* fwDataB64 = cmdJson["data"].as<const char*>();
          uint32_t decodedSize = Base64::decodeBase64Length(reinterpret_cast<const uint8_t*>(fwDataB64));
          if(fwDataSize != decodedSize) {
            if(serialPort) { serialPort->printf_P(PSTR("%sFW piece size check error!\r\n"), COMMON_PREFIX); }
            return;
          }
          Base64::decodeBase64(reinterpret_cast<const uint8_t*>(fwDataB64), fwData, strlen(fwDataB64));
          if(!ota.store(fwPieceNumber, fwData, decodedSize)) {
            if(serialPort) { serialPort->printf_P(PSTR("%sFW storing failed!\r\n"), COMMON_PREFIX); }
          }
        } break;
        case Command::OTA_END: {
          if(!ota.checkValidity()) {
            if(serialPort) { serialPort->printf_P(PSTR("%sStored FW is not valid!\r\n"), COMMON_PREFIX); }
          }
        } break;
        case Command::OTA_STOP: {  } break;
      };
    }
  }

  Common(const Common&) = delete;                       // Define copy constructor.
  Common& operator=(const Common&) = delete;            // Define copy assignment operator.
  Common(Common&&) = delete;                            // Define move constructor.
  Common& operator=(Common&&) = delete;                 // Define move assignment operator.
private:
  Stream* serialPort;
  OTA ota;

  static const char PROGMEM COMMON_PREFIX[];
};

const char Common::COMMON_PREFIX[] PROGMEM              = "[COMMON] ";

#endif // COMMON_HPP