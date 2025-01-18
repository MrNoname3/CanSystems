#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "canHandler.hpp"                                           /// CAN handler library.
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "crc16.hpp"                                                /// CRC16 calculator class.
#include <LittleFS.h>                                               /// Use FLASH file system.
#include "common.hpp"                                               /// Common definitions and functions.

class CanMqttGateway;

class CanOta final {
private:
  static constexpr uint32_t otaTimeoutTime = Time::minToMs(2U);
  static constexpr uint8_t fileNameSize = 32U;                      // Maximum length of the file name.
  static constexpr uint8_t readBufferSize = 64U;
  static constexpr uint8_t filePieceSize = 4U;
  static constexpr const uint8_t otaFrameBufSize = 64U;
  using OtaStartErrorType = uint16_t;

  static inline const char PROGMEM otaFrame[] = {
    "{"
      "\"OTA\":\"%s\""
    "}"
  };

  enum class OtaStartError : OtaStartErrorType {
    NONE                  = 0U,                   // No error.
    FILE_NAME_NULLPTR     = 1 << 0U,
    SUBTOPIC_NULLPTR      = 1 << 1U,
    FILE_NAME_STR_INVALID = 1 << 2U,
    FILE_NAME_STR_EMPTY   = 1 << 3U,
    FILE_NOT_EXISTS       = 1 << 4U,
    CANNOT_OPEN_FILE      = 1 << 5U,
    FILE_EMPTY            = 1 << 6U,
    OTA_IN_PROGRESS       = 1 << 7U,
    EMPTY_OBJECT          = 1 << 8U
  };

public:
  explicit CanOta(CanMqttGateway& canMqttGateway);

  ~CanOta();

  OtaStartErrorType startOta(const char* fileName, uint16_t storageNumber = 0U);

  inline bool isOtaInProgress() { return transferState != TransferState::IDLE; }

  void handleOtaCanFrames(const CanHandler::CanFrame& canFrame);

  void runOta();

  CanOta(const CanOta&) = delete;                       // Define copy constructor.
  CanOta& operator=(const CanOta&) = delete;            // Define copy assignment operator.
  CanOta(CanOta&&) = delete;                            // Define move constructor.
  CanOta& operator=(CanOta&&) = delete;                 // Define move assignment operator

private:
  enum class TransferState : uint8_t {
    IDLE = 0U,
    WAIT_FOR_ACK,
    START,
    STORE,
    VALID,
    INVALID
  };

  CanMqttGateway& canMqttGateway;
  File receivedFile;
  uint32_t frameNumber;
  uint16_t storageNumber;
  uint32_t fileSize;
  TransferState transferState;
  Crc16 crc16;
  uint32_t otaTimeoutTimer;
  char fileNameLocal[fileNameSize];
};

class CanMqttGateway : public CanBase, public MqttBase {
private:
  static constexpr uint32_t clientPingTime = Time::secToMs(1U);
  static constexpr uint32_t clientOfflineTime = Time::secToMs(2U);
  static constexpr uint8_t statusFrameBufSize = 24U;
  static constexpr uint8_t buttonFrameBufSize = 24U;
  static constexpr uint8_t buildInfoFrameBufSize = 96U;

  static inline const char PROGMEM statusOnline[]     = "ONLINE";
  static inline const char PROGMEM statusOffline[]    = "OFFLINE";
  static inline const char PROGMEM statusRestarted[]  = "RESTARTED";
  static inline const char PROGMEM statusFrame[] = {
    "{"
      "\"Status\":\"%s\""
    "}"
  };
  static inline const char PROGMEM buttonFrame[] = {
    "{"
      "\"Button\":%hu"
    "}"
  };
  static inline const char PROGMEM buildInfoFrame[] = {
    "{"
      "\"Firmware\":%hu,"
      "\"GitHash\":\"%x\""
    "}"
  };

public:

  virtual void processMessageArrived(JsonDocument& payloadJson) = 0;

  virtual void processCanFrameArrived(const CanHandler::CanFrame& canFrame) = 0;

  CanMqttGateway(const CanMqttGateway&) = delete;                       // Define copy constructor.
  CanMqttGateway& operator=(const CanMqttGateway&) = delete;            // Define copy assignment operator.
  CanMqttGateway(CanMqttGateway&&) = delete;                            // Define move constructor.
  CanMqttGateway& operator=(CanMqttGateway&&) = delete;                 // Define move assignment operator

protected:
  CanMqttGateway(CanHandler& canHandler, uint16_t clientCanId,
    Connectivity& connectivity, const char* subTopic);

  /// @brief Virtual destructor of the object.
  virtual ~CanMqttGateway() = default;

private:
  virtual bool init() override;

  virtual bool run() override;

  void handlePing();

  [[nodiscard]] virtual bool initLocal() = 0;

  [[nodiscard]] virtual bool runLocal() = 0;

  virtual void messageArrivedCallback(JsonDocument& payloadJson) override;

  virtual void canFrameArrivedCallback(const CanHandler::CanFrame& canFrame) override;

  CanOta canOta;
  uint32_t clientPingTimer;
  uint32_t clientOfflineTimer;
  bool clientOnline;
};