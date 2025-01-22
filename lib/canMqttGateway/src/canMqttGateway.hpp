#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "canHandler.hpp"                                           /// CAN handler library.
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "crc16.hpp"                                                /// CRC16 calculator class.
#include <LittleFS.h>                                               /// Use FLASH file system.
#include "common.hpp"                                               /// Common definitions and functions.

class CanMqttGateway;                                                   // Forward declaration.

/// @brief Manages the Over-The-Air (OTA) firmware updates for CAN devices.
class CanOta final {
private:
  static constexpr uint32_t otaTimeoutTime = Time::minToMs(2U);         // Timeout for OTA operations in milliseconds.
  static constexpr uint8_t fileNameSize = 32U;                          // Maximum length of the firmware file name.
  static constexpr uint8_t readBufferSize = 64U;                        // Buffer size for reading file chunks.
  static constexpr uint8_t filePieceSize = 4U;                          // Size of file pieces sent over CAN.
  static constexpr const uint8_t otaFrameBufSize = 16U;                 // Buffer size for OTA status messages.
  using OtaStartErrorType = uint8_t;                                    // Type for representing OTA start errors.

  // JSON template for OTA status messages.
  static constexpr const char PROGMEM otaFrame[] = R"({"OTA":"%s"})";

  /// @brief Error codes for the start of OTA operations.
  enum class OtaStartError : OtaStartErrorType {
    NONE                  = 0U,                   // No error.
    FILE_NAME_NULLPTR     = 1 << 0U,              // Null pointer for the file name.
    FILE_LOCATION_INVALID = 1 << 1U,              // Invalid file location.
    SUBTOPIC_NULLPTR      = 1 << 2U,              // Null pointer for the subtopic.
    FILE_NAME_STR_INVALID = 1 << 3U,              // Invalid file name string.
    FILE_NAME_STR_EMPTY   = 1 << 4U,              // Empty file name string.
    FILE_NOT_EXISTS       = 1 << 5U,              // File does not exist.
    CANNOT_OPEN_FILE      = 1 << 6U,              // Unable to open the file.
    FILE_EMPTY            = 1 << 7U               // File is empty.
  };

public:
  /// @brief Constructs a CanOta object.
  /// @param canMqttGateway Reference to the CanMqttGateway object.
  explicit CanOta(CanMqttGateway& canMqttGateway);

  /// @brief Destructor for the CanOta class. Closes the file if open.
  ~CanOta();

  /// @brief Starts the OTA process.
  /// @param fileName The name of the firmware file.
  /// @param storageNumber The storage location number.
  /// @return A bitmask of OtaStartError indicating any errors encountered.
  OtaStartErrorType startOta(const char* fileName = FileName::getExtOtaFwLocation(), uint16_t storageNumber = 0U);

  /// @brief Checks if an OTA process is currently in progress.
  /// @return True if OTA is in progress, false otherwise.
  inline bool isOtaInProgress() { return transferState != TransferState::IDLE; }

  /// @brief Handles incoming CAN frames related to the OTA process.
  /// @param canFrame The received CAN frame.
  void handleOtaCanFrames(const CanHandler::CanFrame& canFrame);

  /// @brief Executes the main OTA processing logic.
  void runOta();

  CanOta(const CanOta&) = delete;                       // Define copy constructor.
  CanOta& operator=(const CanOta&) = delete;            // Define copy assignment operator.
  CanOta(CanOta&&) = delete;                            // Define move constructor.
  CanOta& operator=(CanOta&&) = delete;                 // Define move assignment operator

private:
  /// @brief  Represents the various states of the OTA process.
  enum class TransferState : uint8_t {
    IDLE = 0U,              // No OTA process is active.
    WAIT_FOR_ACK,           // Waiting for an acknowledgment from the CAN device.
    START,                  // Starting the OTA process.
    STORE,                  // Storing file data to the CAN device.
    VALID,                  // OTA process completed successfully.
    INVALID                 // OTA process encountered an error.
  };

  CanMqttGateway& canMqttGateway;                       // Reference to the CanMqttGateway for communication.
  File receivedFile;                                    // File object for the firmware being transferred.
  uint32_t frameNumber;                                 // Current frame number being processed.
  uint16_t storageNumber;                               // Storage location number for the firmware.
  uint32_t fileSize;                                    // Size of the firmware file.
  TransferState transferState;                          // Current state of the OTA process.
  Crc16 crc16;                                          // CRC16 calculator for validating file integrity.
  uint32_t otaTimeoutTimer;                             // Timer for OTA process timeout.
  char fileNameLocal[fileNameSize];                     // Local copy of the firmware file name.
};

/// @brief Combines CAN and MQTT functionalities for managing device communications.
class CanMqttGateway : public CanBase, public MqttBase {
private:
  static constexpr uint32_t clientPingTime = Time::secToMs(1U);         // Time interval for sending client pings.
  static constexpr uint32_t clientOfflineTime = Time::secToMs(2U);      // Timeout to detect client offline status.
  static constexpr uint8_t statusFrameBufSize = 24U;                    // Buffer size for status messages.
  static constexpr uint8_t buttonFrameBufSize = 16U;                    // Buffer size for button messages.
  static constexpr uint8_t buildInfoFrameBufSize = 60U;                 // Buffer size for build info messages.

  static constexpr const char PROGMEM statusOnline[]     = "ONLINE";    // Status message for online state.
  static constexpr const char PROGMEM statusOffline[]    = "OFFLINE";   // Status message for offline state.
  static constexpr const char PROGMEM statusRestarted[]  = "RESTARTED"; // Status message for restarted state.

  // JSON template for status messages.
  static constexpr const char PROGMEM statusFrame[] = R"({"Status":"%s"})";

  // JSON template for button messages.
  static constexpr const char PROGMEM buttonFrame[] = R"({"Button":%hu})";

  // JSON template for build info messages.
  static constexpr const char PROGMEM buildInfoFrame[] = R"({"Firmware":%hu,"GitHash":"%x","GitDirty":%hu})";

public:
  /// @brief Processes an MQTT message received for this client.
  /// @param payloadJson The JSON document containing the message payload.
  virtual void processMessageArrived(JsonDocument& payloadJson) = 0;

  /// @brief Processes a CAN frame received for this client.
  /// @param canFrame The received CAN frame.
  virtual void processCanFrameArrived(const CanHandler::CanFrame& canFrame) = 0;

  CanMqttGateway(const CanMqttGateway&) = delete;                       // Define copy constructor.
  CanMqttGateway& operator=(const CanMqttGateway&) = delete;            // Define copy assignment operator.
  CanMqttGateway(CanMqttGateway&&) = delete;                            // Define move constructor.
  CanMqttGateway& operator=(CanMqttGateway&&) = delete;                 // Define move assignment operator

protected:
  /// @brief Constructs a CanMqttGateway object.
  /// @param canHandler Reference to the CAN handler.
  /// @param clientCanId CAN ID for the client.
  /// @param connectivity Reference to the MQTT connectivity handler.
  /// @param subTopic Subtopic for MQTT communication.
  CanMqttGateway(CanHandler& canHandler, uint16_t clientCanId,
    Connectivity& connectivity, const char* subTopic);

  /// @brief Virtual destructor of the object.
  virtual ~CanMqttGateway() = default;

private:
  /// @brief Initializes the gateway.
  /// @return True on successful initialization.
  virtual bool init() override;

  /// @brief Runs the gateway logic.
  /// @return True if successful, false otherwise.
  virtual bool run() override;

  /// @brief Handles client ping logic.
  void handlePing();

  /// @brief Local initialization logic to be implemented by derived classes.
  /// @return True if successful, false otherwise.
  [[nodiscard]] virtual bool initLocal() = 0;

  /// @brief Local runtime logic to be implemented by derived classes.
  /// @return True if successful, false otherwise.
  [[nodiscard]] virtual bool runLocal() = 0;

  /// @brief Handles the arrival of an MQTT message.
  /// @param payloadJson The JSON document containing the message payload.
  virtual void messageArrivedCallback(JsonDocument& payloadJson) override;

  /// @brief Handles the arrival of a CAN frame.
  /// @param canFrame The received CAN frame to be processed.
  virtual void canFrameArrivedCallback(const CanHandler::CanFrame& canFrame) override;

  CanOta canOta;                  // CanOta instance responsible for handling OTA updates.
  uint32_t clientPingTimer;       // Timer for tracking the interval of client pings.
  uint32_t clientOfflineTimer;    // Timer for tracking the time since the last client ping to detect offline status.
  bool clientOnline;              // Flag indicating the current online status of the client. True if online, false if offline.
};