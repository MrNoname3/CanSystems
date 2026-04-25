#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "dataTransfer.hpp"                                         /// Provides file transfer functionality via MQTT.
#include "otaRegistry.hpp"                                          /// OTA target registry interface.

/// @brief Provides common MQTT functionalities.
class MqttCommon final : public MqttBase {
private:
  static constexpr uint8_t maxCmdLength   = 16U;                    // Maximum length of a command string.

  static constexpr const char PROGMEM cmdReboot[] = "reboot";       // Command name stored in flash.
  static constexpr const char PROGMEM discEntityFields[] = R"("name":"Reboot","device_class":"restart","payload_press":"{\"cmd\":\"reboot\"}")";           // HA discovery entity fields stored in flash.

public:
  /// @brief Constructs a new MqttCommon object.
  /// @param connectivity Reference to a Connectivity object to manage MQTT connections.
  /// @param subtopic Subtopic for MQTT message routing.
  MqttCommon(Connectivity& connectivity, const char* subtopic);

  /// @brief Destructor of the object.
  ~MqttCommon() override = default;

  /// @brief Initializes the MQTT common functionalities.
  /// @return `true` if initialization is successful, `false` otherwise.
  bool init() override;

  /// @brief Executes periodic tasks for file transfer and command processing.
  /// @return `true` if the run cycle is executed successfully, `false` otherwise.
  bool run() override;

  /// @brief Publishes the HA MQTT discovery config for the reboot button entity.
  /// @return `true` if publishing succeeded; otherwise, `false`.
  bool publishDiscovery() override;

  /// @brief Processes an incoming MQTT message in JSON format.
  /// @param payloadJson JSON document containing the received MQTT message.
  void messageArrivedCallback(JsonDocument& payloadJson) override;

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
  /// @param errCode Optional error code included in the response on failure (0 = no error).
  /// @return `true` if the response is sent successfully, `false` otherwise.
  bool sendResponse(bool result, uint32_t errCode = 0U);

  /// @brief Dispatches an incoming command string to the appropriate handler.
  /// @param cmd Null-terminated command string received via MQTT.
  void dispatchCommand(const char* cmd);

  /// @brief Handles the reboot command by restarting the MCU.
  void handleReboot();

  // --- Command dispatch table types ---

  using CmdHandler = void (MqttCommon::*)();                    // Pointer-to-member type for command handlers.

  /// @brief Entry in the command lookup table, pairing a command name with its handler.
  struct CmdEntry {
    const char* name;                                           // PROGMEM string pointer to the command name.
    CmdHandler  handler;                                        // Pointer to the member function handling the command.
  };

  static const CmdEntry cmdTable[];                             // Lookup table mapping command strings to handlers.

  static inline bool isFileCheckDone = false;       // Flag indicating that a file validity check is completed.
  static inline bool isFileValid = false;           // Flag indicating the result of the file validity check.

  DataTransfer dataTransfer;                        // Handles file transfer operations.
  bool isRestartRequired = false;                   // Flag indicating whether a device restart is required after file transfer.
};
