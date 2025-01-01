#ifndef MQTT_COMMON_HPP
#define MQTT_COMMON_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include <HardwareSerial.h>                                         /// Hardware serial driver for communication with peripheral devices.
#include "dataTransfer.hpp"

class MqttCommon final : public MqttBase {
public:
  enum class Command : uint8_t {
    BLANK = 0,
    RESTART,
    FW_DT_START,
    FW_DT_DATA,
    FW_DT_END,
    WIFICFG_DT_START,
    WIFICFG_DT_DATA,
    WIFICFG_DT_END,
    EXT_FILE_DT_START,
    EXT_FILE_DT_DATA,
    EXT_FILE_DT_END
  };

  MqttCommon(Connectivity& connectivity, const char* classID, HardwareSerial& serial);

  /// @brief Destructor of the object.
  virtual ~MqttCommon() = default;

  virtual void messageArrivedCallback(const uint8_t* payload, uint32_t length) override;

  virtual bool init() override;

  virtual bool run() override;

  MqttCommon(const MqttCommon&) = delete;                       // Define copy constructor.
  MqttCommon& operator=(const MqttCommon&) = delete;            // Define copy assignment operator.
  MqttCommon(MqttCommon&&) = delete;                            // Define move constructor.
  MqttCommon& operator=(MqttCommon&&) = delete;                 // Define move assignment operator.

private:
  HardwareSerial& serial;
  char externalFileName[28];
  DataTransfer dataTransfer;
};
#endif // MQTT_COMMON_HPP