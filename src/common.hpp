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
      const uint8_t cmd = cmdJson["cmd"] | 0;
      Command command = static_cast<Command>(cmd);
      switch(command) {
        case Command::BLANK: {} break;
        case Command::RESTART: { restartESP(); } break;
        case Command::OTA_START: {
          const uint32_t fwSize = cmdJson["fwSize"] | 0;
          const uint32_t fwCrc = cmdJson["crc32"] | 0;
          const bool otaBeginResult = ota.begin(fwSize, fwCrc);
          if(!otaBeginResult) {
            if(serialPort) { serialPort->printf_P(PSTR("%sCan't begin OTA!\r\n"), COMMON_PREFIX); }
            return;
          }
        } break;
        case Command::OTA_DATA: {
          const uint32_t fwPieceNumber = cmdJson["piece"].as<uint32_t>();
          const uint16_t fwDataSize = cmdJson["size"].as<uint16_t>();
          String fwDataB64 = cmdJson["data"].as<String>();

          const uint32_t crc32BE = cmdJson["crc32BE"].as<uint32_t>();
          const uint32_t crc32AE = cmdJson["crc32AE"].as<uint32_t>();

          const uint32_t crc32AE_calc = Crc32::calculate(reinterpret_cast<const uint8_t*>(fwDataB64.c_str()), fwDataB64.length());
          if(crc32AE != crc32AE_calc) {
            Serial.printf_P(PSTR("Crc AE: %u - %u\r\n"), crc32AE, crc32AE_calc);
          }

          if(fwDataB64) {
            uint32_t decodedSize = Base64::decodeBase64Length(reinterpret_cast<const uint8_t*>(fwDataB64.c_str()));
            if(fwDataSize != decodedSize) {
              if(serialPort) { serialPort->printf_P(PSTR("%sFW piece size check error!\r\n"), COMMON_PREFIX); }
              return;
            }
            fwDataB64.concat('\0');
            uint8_t fwData[240];
            Base64::decodeBase64(reinterpret_cast<const uint8_t*>(fwDataB64.c_str()), fwData, fwDataB64.length());

            const uint32_t crc32BE_calc = Crc32::calculate(fwData, fwDataSize);
            if(crc32BE != crc32BE_calc) {
              Serial.printf_P(PSTR("Crc BE: %u - %u\r\n"), crc32BE, crc32BE_calc);
            }

            //Serial.println(String((char*)fwData));
            if(!ota.store(fwPieceNumber, fwData, fwDataSize)) {
              if(serialPort) { serialPort->printf_P(PSTR("%sFW storing failed!\r\n"), COMMON_PREFIX); }
            }
          }
          else {
            if(serialPort) { serialPort->printf_P(PSTR("%sOTA object already terminated!\r\n"), COMMON_PREFIX); }
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