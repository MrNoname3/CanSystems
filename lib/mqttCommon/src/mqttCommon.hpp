#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "dataTransfer.hpp"                                         /// Provides file transfer functionality via MQTT.

/// @brief Provides common MQTT functionalities.
class MqttCommon final : public MqttBase {
private:
  static constexpr uint8_t dataOutBufSize = 68U;                    // Buffer size for outgoing data messages.
  // Format string for version messages.
  static constexpr const char PROGMEM versionMessageFrame[] = R"({"CPP":%u,"FW":%hu,"GH":"%x","Dirty":%hu,"RR":%hu})";

public:
  /// @brief Commands supported by MqttCommon.
  enum class Command : uint8_t {
    RESTART = 1U,               // Restart the device.
    FW_DT_START,                // Start a firmware file transfer.
    FILE_PIECE,                 // Transfer a piece of a file.
    FILE_CHECK,                 // Check file validity.
    WIFICFG_DT_START,           // Start a WiFi configuration file transfer.
    EXT_FILE_DT_START           // Start an external file transfer.
  };

  /// @brief Constructs a new MqttCommon object.
  /// @param connectivity Reference to a Connectivity object to manage MQTT connections.
  /// @param subtopic Subtopic for MQTT message routing.
  MqttCommon(Connectivity& connectivity, const char* subtopic);

  /// @brief Destructor of the object.
  ~MqttCommon() = default;

  /// @brief Initializes the MQTT common functionalities.
  /// @return `true` if initialization is successful, `false` otherwise.
  virtual bool init() override;

  /// @brief Executes periodic tasks for file transfer and command processing.
  /// @return `true` if the run cycle is executed successfully, `false` otherwise.
  virtual bool run() override;

  /// @brief Processes an incoming MQTT message in JSON format.
  /// @param payloadJson JSON document containing the received MQTT message.
  virtual void messageArrivedCallback(JsonDocument& payloadJson) override;

  MqttCommon(const MqttCommon&) = delete;                       // Define copy constructor.
  MqttCommon& operator=(const MqttCommon&) = delete;            // Define copy assignment operator.
  MqttCommon(MqttCommon&&) = delete;                            // Define move constructor.
  MqttCommon& operator=(MqttCommon&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Callback invoked after a file validity check is completed.
  /// @param isValid `true` if the file is valid, `false` otherwise.
  static void fileValidCb(bool isValid);

  /// @brief Sends a response message over MQTT.
  /// @param result Outcome of the previous operation (`true` for success, `false` for failure).
  /// @param command The command that triggered this response.
  /// @return `true` if the response is sent successfully, `false` otherwise.
  bool sendResponse(bool result, Command command);

  static inline bool isFileCheckDone = false;       // Flag indicating that a file validity check is completed.
  static inline bool isFileValid = false;           // Flag indicating the result of the file validity check.

  DataTransfer dataTransfer;                        // Handles file transfer operations.
  bool isRestartRequired;                           // Flag indicating whether a device restart is required after file transfer.
};