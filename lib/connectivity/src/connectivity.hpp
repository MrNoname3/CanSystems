#ifndef CONNECTIVITY_HPP
#define CONNECTIVITY_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <string.h>                                                 /// Methods for string handling.
#ifndef MQTT_MAX_PACKET_SIZE                                        /// Ensure the `MQTT_MAX_PACKET_SIZE` macro is defined.
#error "MQTT_MAX_PACKET_SIZE is not defined in platformio.ini file!"
#endif

static_assert(MQTT_MAX_PACKET_SIZE >= 1024U, "MQTT buffer size is too short (minimum: 1024 bytes)!");

#include "networkManager.hpp"                                       /// Manages the network connection.
#include <pgmspace.h>                                               /// Provides PROGMEM support for storing data in flash memory.
#ifdef ESP8266
#include <optional>                                                 /// Optional type for conditional storage.
#endif
#include <WiFiClientSecure.h>                                       /// Provides a TCP client with SSL/TLS support.
#include <PubSubClient.h>                                           /// Lightweight MQTT client library for embedded systems.
#include "common.hpp"                                               /// Common definitions and functions.
#include "configHandler.hpp"                                        /// Retrieves configurations from file system.
#include "taskHandler.hpp"                                          /// Class for task scheduling.
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "mqttTopics.hpp"                                           /// MQTT topic format strings and derived buffer sizes.
#include "haDiscovery.hpp"                                          /// Home Assistant MQTT auto-discovery handler.
#include "sync.hpp"                                                 /// RecursiveMutex/LockGuard (no-op off-ESP32).

class MqttBase;                                                     // Forward declaration.

/// @brief Manages network and MQTT connectivity for the system.
class Connectivity final : public Task {
public:
  /// @brief Type alias exposing HADiscovery as a nested name for backward compatibility with handlers.
  using HADiscovery = ::HADiscovery;

private:
  static constexpr uint32_t deviceResetTime = Time::hrToMs(3U);     // Time before the device resets due to being offline.
  static constexpr uint32_t reconnectTime = Time::secToMs(60U);     // Interval for MQTT reconnect retries and WDT-reset backoff.
  static constexpr uint8_t dateTimeStrBufSize = 24U;                // Buffer size for ISO8601 date-time strings.
  // clang-format off
  static constexpr const char PROGMEM mqttConnectionTimeoutStr[]      = "MQTT_CONNECTION_TIMEOUT";        // MQTT connection timeout string.
  static constexpr const char PROGMEM mqttConnectionLostStr[]         = "MQTT_CONNECTION_LOST";           // MQTT connection lost string.
  static constexpr const char PROGMEM mqttConnectFailedStr[]          = "MQTT_CONNECT_FAILED";            // MQTT connection failed string.
  static constexpr const char PROGMEM mqttDisconnectedStr[]           = "MQTT_DISCONNECTED";              // MQTT disconnected string.
  static constexpr const char PROGMEM mqttConnectedStr[]              = "MQTT_CONNECTED";                 // MQTT connected string.
  static constexpr const char PROGMEM mqttConnectBadProtocolStr[]     = "MQTT_CONNECT_BAD_PROTOCOL";      // MQTT bad protocol string.
  static constexpr const char PROGMEM mqttConnectBadClientIdStr[]     = "MQTT_CONNECT_BAD_CLIENT_ID";     // MQTT bad client ID string.
  static constexpr const char PROGMEM mqttConnectUnavailableStr[]     = "MQTT_CONNECT_UNAVAILABLE";       // MQTT server unavailable string.
  static constexpr const char PROGMEM mqttConnectBadCredentialsStr[]  = "MQTT_CONNECT_BAD_CREDENTIALS";   // MQTT bad credentials string.
  static constexpr const char PROGMEM mqttConnectUnauthorizedStr[]    = "MQTT_CONNECT_UNAUTHORIZED";      // MQTT unauthorized string.
  static constexpr const char PROGMEM mqttUnknownStatusStr[]          = "MQTT_UNKNOWN_STATUS";            // MQTT unknown status string.
  // clang-format on
public:
  /// @brief Constructs a Connectivity instance.
  /// @param networkManager Reference to the network manager handling WiFi/Ethernet connections.
  /// @param debugLedFunc Function pointer for controlling the debug LED state.
  /// @param resetWdtFunc Function pointer for resetting the watchdog timer.
  Connectivity(NetworkManager& networkManager, void (*debugLedFunc)(bool state), void (*resetWdtFunc)());

  /// @brief Destructor of the object.
  ~Connectivity() override = default;

  /// @brief Initializes the connectivity system.
  /// @return `true` if initialization succeeds; otherwise, `false`.
  [[nodiscard]] bool init() override;

  /// @brief Main execution loop for the connectivity system.
  /// @return `true` if the task executes successfully; otherwise, `false`.
  [[nodiscard]] bool run() override;

  /// @brief Sends a message to the MQTT broker.
  /// @param subTopic The subtopic to publish to.
  /// @param payload The message payload.
  /// @return `true`if the message is published successfully; otherwise, `false`.
  [[nodiscard]] bool sendMqttMessage(const char* subTopic, const char* payload);

  /// @brief Registers a callback for handling incoming MQTT messages.
  /// @param mqttBasePtr Pointer to the MQTT base class handling the callback.
  /// @return `true` if the callback is registered successfully; otherwise, `false`.
  bool registerCallback(MqttBase* mqttBasePtr);

  /// @brief Publishes a Home Assistant MQTT discovery config for any entity type.
  /// Delegates to `HADiscovery::publishEntity()`; unique_id, topic, availability, and device
  /// blocks are filled in automatically from the known connection credentials.
  /// @param subtopic Entity subtopic — used to build unique_id and complete the topic URL.
  /// @param config Entity-specific discovery configuration (see `HADiscovery::EntityConfig`).
  /// @return `true` if the discovery message was published successfully; otherwise, `false`.
  [[nodiscard]] bool publishEntityDiscovery(const char* subtopic, const HADiscovery::EntityConfig& config);

  /// @brief Publishes a retained MQTT message to senderTopic + subSubTopic.
  /// @param subSubTopic Extended subtopic appended to the sender topic base.
  /// @param payload     The message payload.
  /// @return `true` if published successfully; otherwise, `false`.
  [[nodiscard]] bool publishRetained(const char* subSubTopic, const char* payload);

  /// @brief Publishes to an absolute MQTT topic (no sender-topic prefix), under the MQTT mutex.
  /// Connectivity is the sole owner of the PubSubClient; HADiscovery publishes its
  /// `homeassistant/...` discovery topics through this so every publish is serialized.
  /// @param topic    Absolute MQTT topic.
  /// @param payload  The message payload.
  /// @param retained Whether the broker should retain the message.
  /// @return `true` if published successfully; otherwise, `false`.
  [[nodiscard]] bool publishRaw(const char* topic, const char* payload, bool retained);

  /// @brief Publishes the offline availability status and disconnects from the MQTT broker.
  /// Call before a planned restart to avoid leaving a zombie TCP connection in the broker.
  void shutdownMqtt();

  /// @brief Publishes a HA discovery config for a CAN sub-device entity via HADiscovery.
  /// @param subtopic     Entity subtopic.
  /// @param config       Typed entity discovery configuration.
  /// @param canDevConfig CAN device identification struct (RAM strings).
  /// @return `true` if published successfully; otherwise, `false`.
  [[nodiscard]] bool publishCanDeviceEntityDiscovery(const char* subtopic,
                                                     const HADiscovery::EntityConfig& config,
                                                     const HADiscovery::CanDeviceConfig& canDevConfig);

  /// @brief Returns the MQTT sender topic base (e.g. "iot/dtos/aabbccddeeff/"). Valid after init().
  [[nodiscard]] const char* getSenderTopic() const { return mqttCredentials.senderTopic; }

  /// @brief Returns the MQTT client name (e.g. "esp32_can_aabbccddeeff"). Valid after init().
  [[nodiscard]] const char* getClientName() const { return mqttCredentials.clientName; }

  Connectivity(const Connectivity&) = delete;                       // Delete copy constructor.
  Connectivity& operator=(const Connectivity&) = delete;            // Delete copy assignment operator.
  Connectivity(Connectivity&&) = delete;                            // Delete move constructor.
  Connectivity& operator=(Connectivity&&) = delete;                 // Delete move assignment operator.

private:
  /// @brief Holds MQTT connection credentials.
  struct MqttCredentials {
    char userName[ConfigHandler::getMaxMqttUserNameSize()]{};       // MQTT username.
    char password[ConfigHandler::getMaxMqttPasswordSize()]{};       // MQTT password.
    char serverName[ConfigHandler::getMaxMqttServerUrlSize()]{};    // MQTT server URL.
    uint16_t serverPort = 0U;                                       // MQTT server port.
    char clientName[32]{};                                          // MQTT client identifier.
    char senderTopic[MqttTopics::getSenderTopicBufSize()]{};        // MQTT base topic for outgoing messages.
    char receiverTopic[MqttTopics::getReceiverTopicBufSize()]{};    // MQTT topic for incoming messages.
    char availabilityTopic[MqttTopics::getAvailTopicBufSize()]{};   // MQTT availability topic for online/offline signalling.

    /// @brief Initializes all members to default values.
    MqttCredentials() = default;
  };

  /// @brief Establishes a connection to the MQTT broker.
  /// @return `true` if the connection was successfully established; otherwise, `false`.
  bool connectToMqttServer();

  /// @brief Synchronizes the system time using NTP.
  /// @return `true` if synchronisation completed within the timeout; `false` if it timed out.
  [[nodiscard]] bool syncNtpTime();

  /// @brief Resets the watchdog timer.
  void resetWatchdogTimer() const {
    if(resetWdt != nullptr) {
      resetWdt();
    }
  }

  /// @brief Converts MQTT connection state codes to human-readable strings.
  /// @param status The MQTT connection state code.
  /// @return A human-readable string describing the state
  [[nodiscard]] static const char* getMqttStatusStr(PubSubClient::State status);

  NetworkManager& networkManager;                                   // Reference to the network manager.
  WiFiClientSecure tcpClient;                                       // Secure TCP client for MQTT connections.
  PubSubClient mqttClient;                                          // MQTT client instance.
  RecursiveMutex mqttMutex;                                         // Serializes all PubSubClient access across tasks (no-op off-ESP32).
  MqttCredentials mqttCredentials;                                  // MQTT connection credentials.
  bool networkState;                                                // Indicates the network connection state.
  PubSubClient::State mqttState;                                    // MQTT connection state.
  bool onlineState;                                                 // Indicates whether the device is online.
  uint32_t deviceResetTimer;                                        // Timer for detecting prolonged offline states.
  void (*debugLed)(bool state);                                     // Function pointer for controlling the debug LED.
  void (*resetWdt)();                                               // Function pointer for resetting the watchdog timer.
  uint32_t reconnectTimer;                                          // Timer for managing MQTT reconnections.
#ifdef ESP8266
  std::optional<X509List> serverCert;                               // Optional server certificate for SSL on ESP8266.
#endif
  MqttBase* handlerListHead = nullptr;                              // Head of the intrusive linked list of registered MQTT message handlers.
  MqttBase* handlerListTail = nullptr;                              // Tail of the intrusive linked list, kept for O(1) append.
  HADiscovery haDiscovery;                                          // HA auto-discovery handler; holds device name and all HA infrastructure.
};

/// @brief Base class for handling MQTT communication tasks.
class MqttBase : public virtual Task {
private:
  static constexpr uint8_t subtopicSize = 16U;                      // Maximum allowed size for MQTT subtopics.
  static constexpr uint8_t responseBufferSize = 42U;                // Size of the buffer used for generating response messages.

public:
  /// @brief Enumeration for MQTT response types.
  enum class Response : uint8_t {
    NACK = 0U,        // Negative acknowledgment.
    ACK = 1U          // Positive acknowledgment.
  };

  /// @brief Initializes the MQTT base instance.
  /// @return `true` if the initialization was successful; otherwise, `false`.
  [[nodiscard]] bool init() override = 0;

  /// @brief Executes the MQTT task logic.
  /// @return `true` if the task ran successfully; otherwise, `false`.
  [[nodiscard]] bool run() override = 0;

  /// @brief Checks whether a given subtopic is valid.
  /// A subtopic is considered valid if it is non-null and its length is within the allowed range.
  /// @param subTopic Pointer to the subtopic string.
  /// @return `true` if the subtopic is valid; otherwise, `false`.
  [[nodiscard]] static bool isSubtopicValid(const char* subTopic) {
    if(subTopic == nullptr) { return false; }
    const uint32_t subtopicLength = strnlen(subTopic, subtopicSize);
    return ((subtopicLength > 0U) && (subtopicLength < subtopicSize));
  }

  /// @brief Gets the current subtopic associated with the MQTT base instance.
  /// @return Pointer to the subtopic string.
  [[nodiscard]] const char* getSubtopic() const { return subtopic; }

  /// @brief Gets the maximum allowed size for MQTT subtopics.
  /// @return The maximum subtopic size.
  [[nodiscard]] static constexpr uint8_t getSubtopicSize() { return subtopicSize; }

  /// @brief Callback invoked when an MQTT message arrives, with the payload already parsed into a JSON document.
  /// Derived classes must implement this to handle incoming messages.
  /// @param payloadJson Reference to a `JsonDocument` containing the parsed payload of the incoming message.
  virtual void messageArrivedCallback(JsonDocument& payloadJson) = 0;

  /// @brief Sends an MQTT message with the specified payload.
  /// @param payload Pointer to the message payload.
  /// @return `true` if the message was sent successfully; otherwise, `false`.
  [[nodiscard]] bool sendMessage(const char* payload) {
    if(payload == nullptr) { return false; }
    return connectivity.sendMqttMessage(subtopic, payload);
  }

  /// @brief Sends a response message with the specified response type and command.
  /// @param response Response type to be sent (ACK or NACK).
  /// @param command Command identifier associated with the response (default: 0).
  /// @param errCode Error code included in the response; 0 means no error (default: 0).
  /// @return `true` if the response was sent successfully; otherwise, `false`.
  [[nodiscard]] virtual bool sendResponse(Response response, uint16_t command = 0U, uint32_t errCode = 0U) {
    char responseBuffer[responseBufferSize] = { '\0' };
    const int32_t responseBufferActualSize = snprintf_P(responseBuffer, sizeof(responseBuffer), PSTR(R"({"type":%hu,"cmd":%hu,"err":%u})"), static_cast<uint16_t>(response), command, errCode);
    const bool responseBufferValid = ((responseBufferActualSize >= 0) && (responseBufferActualSize < static_cast<int32_t>(sizeof(responseBuffer))));
    if(!responseBufferValid) { return false; }
    return sendMessage(responseBuffer);
  }

  /// @brief Called on every MQTT connect to publish the HA discovery config for this entity.
  /// Override in derived classes that expose HA entities; the default no-op is safe for handlers
  /// that do not need HA discovery (e.g. file-transfer handlers).
  /// @return `true` if publishing succeeded or no discovery is needed; `false` on publish failure.
  virtual bool publishDiscovery() { return true; }

  /// @brief Publishes the HA MQTT discovery config for this entity via `HADiscovery::publishEntity`.
  /// Call from a `publishDiscovery()` override using one of the `EntityConfig` factory methods.
  /// @param config Typed entity discovery configuration built via `EntityConfig::sensor()` etc.
  /// @return `true` if the message was sent successfully; otherwise, `false`.
  [[nodiscard]] bool doPublishEntityDiscovery(const HADiscovery::EntityConfig& config) {
    return connectivity.publishEntityDiscovery(subtopic, config);
  }

  /// @brief Publishes a retained MQTT message to senderTopic + subSubTopic.
  /// Use for CAN device availability and info topics (e.g. "alert1/availability", "alert1/info").
  /// @param subSubTopic Extended subtopic (e.g. "alert1/availability") appended to the sender topic.
  /// @param payload     Message payload.
  /// @return `true` if published successfully; otherwise, `false`.
  [[nodiscard]] bool sendRetainedSubtopic(const char* subSubTopic, const char* payload) {
    return connectivity.publishRetained(subSubTopic, payload);
  }

  /// @brief Publishes a non-retained MQTT message to senderTopic + subSubTopic.
  /// Use for CAN device event sub-topics (e.g. "alert1/ota", "alert1/button").
  /// @param subSubTopic Extended subtopic (e.g. "alert1/ota") appended to the sender topic.
  /// @param payload     Message payload.
  /// @return `true` if published successfully; otherwise, `false`.
  [[nodiscard]] bool sendSubtopicMessage(const char* subSubTopic, const char* payload) {
    return connectivity.sendMqttMessage(subSubTopic, payload);
  }

  /// @brief Publishes a HA discovery config for a CAN sub-device entity.
  /// @param subtopic     Entity subtopic (e.g. "temperature").
  /// @param config       Entity discovery configuration.
  /// @param canDevConfig CAN device identification and availability data.
  /// @return `true` if published successfully; otherwise, `false`.
  [[nodiscard]] bool doPublishCanDeviceEntityDiscovery(const char* subtopic,
                                                       const HADiscovery::EntityConfig& config,
                                                       const HADiscovery::CanDeviceConfig& canDevConfig) {
    return connectivity.publishCanDeviceEntityDiscovery(subtopic, config, canDevConfig);
  }

  /// @brief Publishes the offline availability status and disconnects from the MQTT broker.
  /// Call before a planned restart to avoid leaving a zombie TCP connection in the broker.
  void shutdownMqtt() { connectivity.shutdownMqtt(); }

  /// @brief Returns the MQTT sender topic base. Valid after Connectivity::init().
  [[nodiscard]] const char* getSenderTopicStr() const { return connectivity.getSenderTopic(); }

  /// @brief Returns the MQTT client name. Valid after Connectivity::init().
  [[nodiscard]] const char* getClientNameStr() const { return connectivity.getClientName(); }

  /// @brief Returns the next handler in the intrusive linked list managed by Connectivity.
  [[nodiscard]] MqttBase* getNextHandler() const { return nextHandler; }

  /// @brief Sets the next handler pointer. Used internally by Connectivity to build the handler list.
  void setNextHandler(MqttBase* next) { nextHandler = next; }

  MqttBase(const MqttBase&) = delete;                       // Delete copy constructor.
  MqttBase& operator=(const MqttBase&) = delete;            // Delete copy assignment operator.
  MqttBase(MqttBase&&) = delete;                            // Delete move constructor.
  MqttBase& operator=(MqttBase&&) = delete;                 // Delete move assignment operator.

protected:
  /// @brief Constructs the MQTT base instance.
  /// @param connectivity Reference to the connectivity object managing MQTT operations.
  /// @param subTopic Pointer to the subtopic string to be associated with the instance.
  MqttBase(Connectivity& connectivity, const char* subTopic) :
    connectivity(connectivity) {
    if(isSubtopicValid(subTopic)) {
      strlcpy(subtopic, subTopic, subtopicSize);
      this->connectivity.registerCallback(this);
    }
  }

  /// @brief Virtual destructor of the object.
  ~MqttBase() override = default;

private:
  Connectivity& connectivity;       // Reference to the connectivity object used for MQTT communication.
  char subtopic[subtopicSize]{};    // Buffer storing the subtopic associated with the MQTT base instance.
  MqttBase* nextHandler = nullptr;  // Intrusive linked list pointer, managed by Connectivity.
};
#endif
