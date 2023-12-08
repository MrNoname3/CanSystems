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

  Common(const char* classID, Stream* serial = nullptr) : MqttComBase(classID), serialPort(serial), ota(nullptr) {}

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
      const uint8_t cmd = cmdJson["cmd"] | 0;
      Command command = static_cast<Command>(cmd);
      switch(command) {
        case Command::BLANK: {} break;
        case Command::RESTART: { restartESP(); } break;
        case Command::OTA_START: {
          const uint32_t fwSize = cmdJson["fwSize"] | 0;
          const uint32_t fwCrc = cmdJson["crc32"] | 0;
          if(!ota) {
            ota = new OTA(fwSize, fwCrc, serialPort);
          }
          else {
            if(serialPort) { serialPort->printf_P(PSTR("%sOTA object already exists!\r\n"), COMMON_PREFIX); }
          }
        } break;
        case Command::OTA_DATA: {
          const uint32_t fwPieceNumber = cmdJson["piece"].as<uint32_t>();
          const uint16_t fwDataSize = cmdJson["size"].as<uint16_t>();
          String fwDataB64 = cmdJson["data"].as<String>();

          if(ota && fwDataB64) {
            uint32_t decodedSize = Base64::decodeBase64Length(reinterpret_cast<const uint8_t*>(fwDataB64.c_str()));
            if(fwDataSize != decodedSize) {
              if(serialPort) { serialPort->printf_P(PSTR("%sFW piece size check error!\r\n"), COMMON_PREFIX); }
              return;
            }
            uint8_t fwData[288];
            Base64::decodeBase64(reinterpret_cast<const uint8_t*>(fwDataB64.c_str()), fwData, fwDataSize);
            //Serial.println(String((char*)fwData));
            if(!ota->store(fwPieceNumber, fwData, fwDataSize)) {
              if(serialPort) { serialPort->printf_P(PSTR("%sFW storing failed!\r\n"), COMMON_PREFIX); }
            }
          }
          else {
            if(serialPort) { serialPort->printf_P(PSTR("%sOTA object already terminated!\r\n"), COMMON_PREFIX); }
          }
        } break;
        case Command::OTA_END: {
          if(ota) {
            if(!ota->checkValidity()) {
              if(serialPort) { serialPort->printf_P(PSTR("%sStored FW is not valid!\r\n"), COMMON_PREFIX); }
            }
            delete ota;
          }
          else {
            if(serialPort) { serialPort->printf_P(PSTR("%sOTA object already terminated!\r\n"), COMMON_PREFIX); }
          }
        } break;
        case Command::OTA_STOP: { if(ota) { delete ota; } } break;
      };
    }
  }

  Common(const Common&) = delete;                       // Define copy constructor.
  Common& operator=(const Common&) = delete;            // Define copy assignment operator.
  Common(Common&&) = delete;                            // Define move constructor.
  Common& operator=(Common&&) = delete;                 // Define move assignment operator.
private:
  Stream* serialPort;
  OTA* ota;

  static const char PROGMEM COMMON_PREFIX[];
};

const char Common::COMMON_PREFIX[] PROGMEM              = "[COMMON] ";

#endif // COMMON_HPP