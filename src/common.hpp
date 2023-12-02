#ifndef COMMON_HPP
#define COMMON_HPP

#include "mqttComBase.hpp"
#include <Arduino.h>                          /// Arduino libraries header.
#include <ArduinoJson.h>                      /// Handle JSON files.

class Common : public MqttComBase {
public:
  enum class Command : uint8_t {
    BLANK = 0,
    RESTART,
    OTA_START,
    OTA_DATA,
    OTA_STOP
  };

  Common(const char* classID, Stream* serial = nullptr) : MqttComBase(classID), serialPort(serial) {}

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
    StaticJsonDocument<64> cmdJson;
    DeserializationError deserializationError = deserializeJson(cmdJson, payload, length);
    const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
    if(deSerResult) {
      const uint8_t cmd = cmdJson["command"] | 0;
      Command command = static_cast<Command>(cmd);
      switch(command) {
        case Command::BLANK: {} break;
        case Command::RESTART: { restartESP(); } break;
      };
    }
  }

  Common(const Common&) = delete;                       // Define copy constructor.
  Common& operator=(const Common&) = delete;            // Define copy assignment operator.
  Common(Common&&) = delete;                            // Define move constructor.
  Common& operator=(Common&&) = delete;                 // Define move assignment operator.
private:
  Stream* serialPort;

  static const char PROGMEM COMMON_PREFIX[];
};

const char Common::COMMON_PREFIX[] PROGMEM              = "[COMMON] ";

#endif // COMMON_HPP