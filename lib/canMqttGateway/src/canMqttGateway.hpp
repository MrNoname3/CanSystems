#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "canHandler.hpp"                                           /// CAN handler library.
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "crc16.hpp"                                                /// CRC16 calculator class.
#include <LittleFS.h>                                               /// Use FLASH file system.
#include "common.hpp"                                               /// Common definitions and functions.
#include "otaRegistry.hpp"                                          /// OTA target registry interface.

class CanMqttGateway;                                                   // Forward declaration.

/// @brief Manages the Over-The-Air (OTA) firmware updates for CAN devices.
class CanOta final {
private:
  static constexpr uint32_t otaTimeoutTime = Time::minToMs(5U);         // Timeout for OTA operations in milliseconds.
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
    FILE_LOCATION_INVALID = 1 << 1U,              // Invalid file location (must start with '/').
    FILE_OPEN_FAILED      = 1 << 2U,              // Unable to open the file.
    FILE_EMPTY            = 1 << 3U               // File is empty.
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
  OtaStartErrorType startOta(const char* fileName, uint16_t storageNumber = 0U);

  /// @brief Checks if an OTA process is currently in progress.
  /// @return True if OTA is in progress, false otherwise.
  [[nodiscard]] inline bool isOtaInProgress() const { return transferState != TransferState::IDLE; }

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
  const char* fileNamePtr;                              // Pointer to the firmware file name (must outlive the OTA process).
};

/// @brief Combines CAN and MQTT functionalities for managing device communications.
class CanMqttGateway : public CanBase, public MqttBase, public OtaTarget {
private:
  static constexpr uint32_t clientPingTime = Time::secToMs(1U);         // Time interval for sending client pings.
  static constexpr uint32_t clientOfflineTime = Time::secToMs(5U);      // Timeout to detect client offline status.
  static constexpr uint8_t buttonFrameBufSize = 16U;                    // Buffer size for button messages.

  // JSON template for button messages.
  static constexpr const char PROGMEM buttonFrame[] = R"({"Button":%hu})";

public:
  /// @brief Processes an MQTT message received for this client.
  /// @param payloadJson The JSON document containing the message payload.
  virtual void processMessageArrived(JsonDocument& payloadJson) = 0;

  /// @brief Processes a CAN frame received for this client.
  /// @param canFrame The received CAN frame.
  virtual void processCanFrameArrived(const CanHandler::CanFrame& canFrame) = 0;

  /// @brief Starts the OTA firmware update process for this device.
  /// @param fileName The name of the firmware file on LittleFS.
  /// @return True if the OTA process started successfully.
  [[nodiscard]] bool startOta(const char* fileName);

  /// @brief Checks if an OTA process is currently in progress on this device.
  /// @return True if OTA is in progress, false otherwise.
  [[nodiscard]] bool isOtaInProgress() const;

  /// @brief Returns the firmware file name configured for this device (PROGMEM pointer).
  [[nodiscard]] const char* getFwFileName() const override { return fwFileNamePtr; }

  /// @brief Triggers OTA using the configured firmware file name.
  void triggerOta() override { (void)startOta(fwFileNamePtr); }

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
  /// @param fwFileName PROGMEM pointer to the firmware file name for OTA triggering (nullptr = no auto OTA).
  CanMqttGateway(CanHandler& canHandler, uint16_t clientCanId,
    Connectivity& connectivity, const char* subTopic, const char* fwFileName = nullptr);

  /// @brief Virtual destructor of the object.
  ~CanMqttGateway() override = default;

private:
  /// @brief Initializes the gateway.
  /// @return True on successful initialization.
  bool init() override;

  /// @brief Runs the gateway logic.
  /// @return True if successful, false otherwise.
  bool run() override;

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
  void messageArrivedCallback(JsonDocument& payloadJson) override;

  /// @brief Handles the arrival of a CAN frame.
  /// @param canFrame The received CAN frame to be processed.
  void canFrameArrivedCallback(const CanHandler::CanFrame& canFrame) override;

  CanOta canOta;                  // CanOta instance responsible for handling OTA updates.
  uint32_t clientPingTimer;       // Timer for tracking the interval of client pings.
  uint32_t clientOfflineTimer;    // Timer for tracking the time since the last client ping to detect offline status.
  bool clientOnline;              // Flag indicating the current online status of the client. True if online, false if offline.
  const char* fwFileNamePtr;      // PROGMEM pointer to the configured firmware file name (nullptr if no auto OTA).

protected:
  /// @brief Builds CAN topic and device metadata buffers from senderTopic, clientName, and subtopic.
  /// Idempotent: if already built (canTopicsBuilt == true), returns immediately.
  void buildCanTopics();

  char canAvailTopic[MqttTopics::getAvailTopicBufSize() + MqttBase::getSubtopicSize()]{};  // "iot/dtos/<mac>/<subtopic>/availability" + null.
  char canInfoTopic[MqttTopics::getInfoTopicBufSize()  + MqttBase::getSubtopicSize()]{};  // "iot/dtos/<mac>/<subtopic>/info" + null.
  char canSwVersion[18]{};        // CAN device sw version string:     "65535 (ffffffff)" + null.
  char canDeviceId[48]{};         // CAN device unique identifier:     "<clientName>_<subtopic>" (max 31+1+15+1=48).
  char canDeviceName[MqttBase::getSubtopicSize() + 7U]{};  // UPPERCASE(subtopic)(max 15) + ' ' + MAC6 + null.
  bool canTopicsBuilt = false;    // True after buildCanTopics() completes successfully.

  static constexpr const char PROGMEM canHwVersionStr[] = "ATmega328P";  // Hardware version string for CAN sub-devices.

  [[nodiscard]] const char* getCanAvailTopic()  const { return canAvailTopic; }
  [[nodiscard]] const char* getCanInfoTopic()   const { return canInfoTopic; }
  [[nodiscard]] const char* getCanSwVersion()   const { return canSwVersion; }
  [[nodiscard]] const char* getCanDeviceId()    const { return canDeviceId; }
  [[nodiscard]] const char* getCanDeviceName()  const { return canDeviceName; }
};