#ifndef CONNECTIVITY_HPP
#define CONNECTIVITY_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <string.h>
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
#include "common.hpp"                                               /// Common definitions and functions.
#include "configHandler.hpp"
#include "taskHandler.hpp"                                          /// Class for task scheduling.

class MqttBase;

class Connectivity final : public Task {
public:
  Connectivity(HardwareSerial& serial, NetworkManager& networkManager, void (*debugLedFunc)(bool state), void (*resetWdtFunc)());

  /// @brief Destructor of the object.
  ~Connectivity() = default;

  virtual bool init() override;

  virtual void run() override;

  bool sendMqttMessage(const char* subTopic, const char* payload);

  bool registerCallback(MqttBase* obj);

private:
  bool connect();

  void syncNtpTime();

  bool getIsoTimeString(char (&dateTimeBuffer)[24U]);

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
    char clientName[32];
    char senderTopic[28];
    char receiverTopic[28];
    MqttCredentials() : userName{'\0'}, password{'\0'}, serverName{'\0'}, serverPort(0), clientName{'\0'}, senderTopic{'\0'}, receiverTopic{'\0'} {}
  };

  static constexpr uint32_t deviceResetTime = Time::hrToMs(3U);

#ifdef ESP8266
  std::optional<X509List> serverCert;
#endif
  HardwareSerial& serialPort;
  NetworkManager& networkManager;
  WiFiClientSecure tcpClient;
  PubSubClient mqttClient;
  MqttCredentials mqttCredentials;
  bool networkState;
  int8_t mqttState;
  bool onlineState;
  uint32_t deviceResetTimer;
  void (*debugLed)(bool state);
  void (*resetWdt)();
  uint8_t subtopicOffset;
  std::vector<MqttBase*> messageHandlerList;

  static const char PROGMEM mqttClientName[];
  static const char PROGMEM mqttOutTopic[];
  static const char PROGMEM mqttInTopic[];

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
};

class Connectivity;

class MqttBase {
private:
  static constexpr uint8_t subtopicSize = 16U;
  static constexpr uint8_t responseBufferSize = 28U;
public:
  enum class Response : uint8_t {
    NACK = 0U,
    ACK
  };

  MqttBase(Connectivity& connectivity, const char* subTopic) :
    connectivity(connectivity),
    subtopic{'\0'}
  {
    if(isSubtopicValid(subTopic)) {
      strlcpy(subtopic, subTopic, subtopicSize);
      connectivity.registerCallback(this);
    }
  }

  virtual ~MqttBase() = default;

  [[nodiscard]] virtual bool init() = 0;

  virtual void run() = 0;

  virtual void messageArrivedCallback(const uint8_t* payload, uint32_t length) = 0;

  [[nodiscard]] static inline bool isSubtopicValid(const char* subTopic) {
    if(subTopic == nullptr) { return false; }
    const uint32_t subtopicLength = strnlen(subTopic, subtopicSize);
    return ((subtopicLength > 0U) && (subtopicLength < subtopicSize));
  }

  [[nodiscard]] static constexpr uint8_t getSubtopicSize () { return subtopicSize; }

  [[nodiscard]] inline const char* getSubtopic() const { return subtopic; }

  [[nodiscard]] inline bool sendMessage(const char* payload) {
    if(payload == nullptr) { return false; }
    return connectivity.sendMqttMessage(getSubtopic(), payload);
  }

  [[nodiscard]] virtual bool sendResponse(Response response, uint16_t command) {
    char responseBuffer[responseBufferSize] = { '\0' };
    const int32_t responseBufferActualSize = snprintf_P(responseBuffer, sizeof(responseBuffer),
      PSTR("{""\"type\":%hu,""\"cmd\":%hu""}"), static_cast<uint8_t>(response), command);
    const bool responseBufferValid = ((responseBufferActualSize >= 0) &&
      (responseBufferActualSize < static_cast<int32_t>(sizeof(responseBuffer))));
    if(!responseBufferValid) { return false; }
    return sendMessage(responseBuffer);
  }

  MqttBase(const MqttBase&) = delete;                       // Define copy constructor.
  MqttBase& operator=(const MqttBase&) = delete;            // Define copy assignment operator.
  MqttBase(MqttBase&&) = delete;                            // Define move constructor.
  MqttBase& operator=(MqttBase&&) = delete;                 // Define move assignment operator

private:
  Connectivity& connectivity;
  char subtopic[subtopicSize];
};
#endif