#ifndef CONNECTIVITY_HPP
#define CONNECTIVITY_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#ifndef MQTT_MAX_PACKET_SIZE                                        /// Ensure the `MQTT_MAX_PACKET_SIZE` macro is defined.
#error "MQTT_MAX_PACKET_SIZE is not defined!"
#endif

static constexpr uint16_t ALLOWED_MQTT_PACKET_SIZE = 1024U;         // Minimum allowed MQTT packet size for proper operation.

/// @brief Static assertion to validate `MQTT_MAX_PACKET_SIZE`.
static_assert(MQTT_MAX_PACKET_SIZE >= ALLOWED_MQTT_PACKET_SIZE, "MQTT buffer size is too short!");

#include "networkManager.hpp"                                       /// Manages the network connection.
#include <pgmspace.h>                                               /// Provides PROGMEM support for storing data in flash memory.
#ifdef ESP8266
#include <optional>
#endif
#include <WiFiClientSecure.h>                                       /// Provides a TCP client with SSL/TLS support.
#include <PubSubClient.h>                                           /// Lightweight MQTT client library for embedded systems.
#include <HardwareSerial.h>                                         /// Hardware serial driver for communication with peripheral devices.
#include <vector>                                                   /// STL vector for dynamic arrays.
#include "debugLedHandler.hpp"                                      /// Handles the debug LED.
#include "common.hpp"                                               /// Common definitions and functions.
#include "dataTransfer.hpp"
#include "configHandler.hpp"
#include "taskHandler.hpp"                                          /// Class for task scheduling.

class Connectivity final : public Task {
public:
  class MqttComBase;

  Connectivity(HardwareSerial& serial, DebugLedHandler& debugLed, NetworkManager& networkManager, void (*resetWdt)());

  /// @brief Destructor of the object.
  ~Connectivity() = default;

  virtual bool init() override;

  virtual void run() override;

private:
  bool connect();

  void receiveMqttMessage(const char* topic, uint8_t* payload, uint32_t length);

  void sendMqttMessage(const char* subTopic, const char* payload);

  void syncNtpTime();
  bool getIsoTimeString(char (&dateTimeBuffer)[24U]);

  bool registerCallback(Connectivity::MqttComBase* obj);

  inline void resetWatchdogTimer() {
    if(resetWdt != nullptr) {
      resetWdt();
    }
  }

  const char* getMqttStatusStr(int8_t status);

public:
  Connectivity(const Connectivity&) = delete;                       // Define copy constructor.
  Connectivity& operator=(const Connectivity&) = delete;            // Define copy assignment operator.
  Connectivity(Connectivity&&) = delete;                            // Define move constructor.
  Connectivity& operator=(Connectivity&&) = delete;                 // Define move assignment operator.

private:
  struct MqttCredentials {
    char userName[ConfigHandler::getMaxMqttUserNameSize()];
    char password[ConfigHandler::getMaxMqttPasswordSize()];
    char serverName[ConfigHandler::getMaxMqttServerUrlSize()];
    uint16_t serverPort;
    char clientName[36];
    char senderTopic[32];
    char receiverTopic[32];
    MqttCredentials() : userName{'\0'}, password{'\0'}, serverName{'\0'}, serverPort(0), clientName{'\0'}, senderTopic{'\0'}, receiverTopic{'\0'} {}
  };

  static constexpr uint32_t deviceResetTime = Time::hrToMs(3U);

#ifdef ESP8266
  std::optional<X509List> serverCert;
#endif
  HardwareSerial& serialPort;
  DebugLedHandler& debugLed;
  NetworkManager& networkManager;
  WiFiClientSecure tcpClient;
  PubSubClient mqttClient;
  MqttCredentials mqttCredentials;
  bool networkState;
  int8_t mqttState;
  bool onlineState;
  uint32_t deviceResetTimer;
  void (*resetWdt)();
  std::vector<Connectivity::MqttComBase*> messageMap;

  static const char PROGMEM BASE_TOPIC[];
  static const char PROGMEM SENDER_TOPIC[];
  static const char PROGMEM RECEIVER_TOPIC[];

  static const char PROGMEM MQTT_CONNECTION_TIMEOUT_STR[];
  static const char PROGMEM MQTT_CONNECTION_LOST_STR[];
  static const char PROGMEM MQTT_CONNECT_FAILED_STR[];
  static const char PROGMEM MQTT_DISCONNECTED_STR[];
  static const char PROGMEM MQTT_CONNECTED_STR[];
  static const char PROGMEM MQTT_CONNECT_BAD_PROTOCOL_STR[];
  static const char PROGMEM MQTT_CONNECT_BAD_CLIENT_ID_STR[];
  static const char PROGMEM MQTT_CONNECT_UNAVAILABLE_STR[];
  static const char PROGMEM MQTT_CONNECT_BAD_CREDENTIALS_STR[];
  static const char PROGMEM MQTT_CONNECT_UNAUTHORIZED_STR[];
  static const char PROGMEM MQTT_UNKNOWN_STATUS_STR[];

public:
  class MqttComBase {
  public:
    friend class Connectivity;
    enum class Response : uint8_t {
      NACK = 0,
      ACK
    };
    MqttComBase(const MqttComBase&) = delete;                       // Define copy constructor.
    MqttComBase& operator=(const MqttComBase&) = delete;            // Define copy assignment operator.
    MqttComBase(MqttComBase&&) = delete;                            // Define move constructor.
    MqttComBase& operator=(MqttComBase&&) = delete;                 // Define move assignment operator.
  protected:
    MqttComBase(Connectivity& connectivity, const char* classID);
    virtual ~MqttComBase() = default;
    void messageSend(const char* payload) const;
    virtual bool sendResponse(Response resp, uint16_t cmd);
    virtual void messageReceived(uint8_t* payload, uint32_t length) = 0;
    virtual bool begin() = 0;
    virtual bool loop() = 0;
    const char* getClassId() const;
    Connectivity& conn;
  private:
    char classId[16];
  };

private:
  class Common final : public Connectivity::MqttComBase {
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

    Common(Connectivity& connectivity, const char* classID);

    /// @brief Destructor of the object.
    virtual ~Common() = default;

    virtual void messageReceived(uint8_t* payload, uint32_t length) override;

    virtual bool begin() override;

    virtual bool loop() override;

    void messageSend(const char* payload) const;

    Common(const Common&) = delete;                       // Define copy constructor.
    Common& operator=(const Common&) = delete;            // Define copy assignment operator.
    Common(Common&&) = delete;                            // Define move constructor.
    Common& operator=(Common&&) = delete;                 // Define move assignment operator.

  private:
    char externalFileName[28];
    DataTransfer dataTransfer;
  };
  Common common;
};
#endif