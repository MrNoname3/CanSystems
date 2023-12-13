#ifndef COMMON_HPP
#define COMMON_HPP

#include "mqttComBase.hpp"
#include <Arduino.h>                          /// Arduino libraries header.
#include "ota.hpp"

class Common : public MqttComBase {
public:
  enum class Command : uint8_t {
    BLANK = 0,
    RESTART,
    OTA_START,
    OTA_DATA,
    OTA_END
  };

  enum class Response : uint8_t {
    NACK = 0,
    ACK,
  };

  Common(const char* classID, Stream* serial = nullptr);

  /// @brief Destructor of the object.
  virtual ~Common() = default;

  /// @brief Reset the MCU.
  void restartESP();

  virtual void messageReceived(uint8_t* payload, uint32_t length) override;

  Common(const Common&) = delete;                       // Define copy constructor.
  Common& operator=(const Common&) = delete;            // Define copy assignment operator.
  Common(Common&&) = delete;                            // Define move constructor.
  Common& operator=(Common&&) = delete;                 // Define move assignment operator.
private:
  Stream* serialPort;
  OTA ota;

  static const char PROGMEM COMMON_PREFIX[];
};
#endif // COMMON_HPP