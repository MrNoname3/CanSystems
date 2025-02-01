#ifndef MQTT_COMMON_HPP
#define MQTT_COMMON_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "dataTransfer.hpp"

class MqttCommon final : public MqttBase {
private:
  static constexpr uint8_t dataOutBufSize = 68U;

  static constexpr const char PROGMEM versionMessageFrame[] = R"({"CPP":%u,"FW":%hu,"GH":"%x","Dirty":%hu,"RR":%hu})";

public:
  enum class Command : uint8_t {
    RESTART = 1U,
    FW_DT_START,
    FILE_PIECE,
    FILE_CHECK,
    WIFICFG_DT_START,
    EXT_FILE_DT_START
  };

  MqttCommon(Connectivity& connectivity, const char* subtopic);

  /// @brief Destructor of the object.
  ~MqttCommon() = default;

  virtual bool init() override;

  virtual bool run() override;

  virtual void messageArrivedCallback(JsonDocument& payloadJson) override;

  MqttCommon(const MqttCommon&) = delete;                       // Define copy constructor.
  MqttCommon& operator=(const MqttCommon&) = delete;            // Define copy assignment operator.
  MqttCommon(MqttCommon&&) = delete;                            // Define move constructor.
  MqttCommon& operator=(MqttCommon&&) = delete;                 // Define move assignment operator.

private:
  static void fileValidCb(bool isValid);

  bool sendResponse(bool result, Command command);

  static inline volatile bool isFileCheckDone = false;
  static inline volatile bool isFileValid = false;

  DataTransfer dataTransfer;
  bool isRestartRequired;
};
#endif // MQTT_COMMON_HPP