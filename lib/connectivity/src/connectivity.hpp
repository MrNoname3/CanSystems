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

class MqttBase;                                                     // Forward declaration.

/// @brief Manages network and MQTT connectivity for the system.
class Connectivity final : public Task {
public:
  /// @brief Handles Home Assistant MQTT auto-discovery.
  /// All HA-specific format strings, buffer sizes, and publish methods live here.
  class HADiscovery {
  public:
    /// @brief Supported Home Assistant MQTT component types.
    enum class EntityType : uint8_t {
      sensor,
      binary_sensor,
      button,
    };

    /// @brief Discovery configuration for a single entity.
    /// `entityFields` should be a `static constexpr const char PROGMEM` member of the handler.
    struct EntityConfig {
      EntityType   type;           // HA component type; resolved to a PROGMEM string internally.
      const char*  entityFields;   // PROGMEM: entity-specific JSON fields; no leading/trailing comma.
      bool isCommandTopic = false; // false: adds state_topic to JSON; true: adds command_topic.
    };

    explicit HADiscovery(Connectivity& conn) : conn(conn) {}
    HADiscovery(const HADiscovery&)             = delete;
    HADiscovery& operator=(const HADiscovery&)  = delete;

    /// @brief Builds the human-readable device name from the deviceId and MAC address.
    /// Stored internally; used in every entity discovery payload as `device.name`.
    /// @param mac 6-byte MAC address array.
    /// @param deviceId Null-terminated device ID string (e.g. "esp32_can").
    void buildDeviceName(const uint8_t mac[6], const char* deviceId);

    /// @brief Publishes the HA MQTT discovery config for any entity type.
    /// Fills in unique_id, state_topic or command_topic, availability, and device blocks
    /// automatically; the handler only supplies the entity-specific JSON fields via `config`.
    /// @param subtopic Entity subtopic — used to build unique_id and complete the topic URL.
    /// @param config Entity-specific discovery configuration.
    /// @return `true` if the discovery message was published successfully; otherwise, `false`.
    [[nodiscard]] bool publishEntity(const char* subtopic, const EntityConfig& config);

    /// @brief Publishes the HA MQTT discovery config for the built-in connectivity binary sensor.
    /// This creates the HA device entry (with device name and sw_version) on every connect.
    /// @return `true` if the discovery message was published successfully; otherwise, `false`.
    bool publishConnectivity();

  private:
    static constexpr uint8_t  discoveryTopicBufSize   = 96U;    // "homeassistant/<type>/<uid>/config" topic buffer.
    static constexpr uint16_t discoveryPayloadBufSize = 512U;   // HA MQTT discovery JSON payload buffer.
    static constexpr uint8_t  swVersionBufSize        = 24U;    // "65535 (ffffffff)" sw version string buffer.
    static constexpr uint8_t  deviceNameBufSize       = 32U;    // "ESP32 CAN A1B2C3" device name buffer.

    // HA component type strings (PROGMEM) — one per EntityType enum value.
    static constexpr const char PROGMEM typeStrSensor[]       = "sensor";
    static constexpr const char PROGMEM typeStrBinarySensor[] = "binary_sensor";
    static constexpr const char PROGMEM typeStrButton[]       = "button";
    // HA discovery format strings (PROGMEM).
    static constexpr const char PROGMEM mqttDiscoveryTopic[]    = "homeassistant/%s/%s_%s/config";
    // Unified payload template — %s args: clientName, subtopic, entityFields, topicFieldName,
    // topicBase, subtopic, availabilityTopic, clientName, deviceName, swVersion.
    static constexpr const char PROGMEM mqttDiscoveryTemplate[] = R"({"unique_id":"%s_%s",%s,"%s":"%s%s","availability":[{"topic":"%s","value_template":"{{ value_json.state }}"}],"device":{"identifiers":["%s"],"name":"%s","sw_version":"%s"}})";
    static constexpr const char PROGMEM connEntityFields[]  = R"("name":"Connection","value_template":"{{ value_json.state }}","payload_on":"online","payload_off":"offline","device_class":"connectivity")"; // Entity fields for the connectivity binary sensor.
    static constexpr const char PROGMEM topicFieldState[]   = "state_topic";    // HA topic field name for outgoing sensor data.
    static constexpr const char PROGMEM topicFieldCmd[]     = "command_topic";  // HA topic field name for inbound commands.

    /// @brief Returns the PROGMEM type string for the given EntityType.
    static constexpr const char* getTypeStr(EntityType type) {
      switch(type) {
        case EntityType::sensor:        return typeStrSensor;
        case EntityType::binary_sensor: return typeStrBinarySensor;
        case EntityType::button:        return typeStrButton;
        default:                        return nullptr;
      }
    }

    /// @brief Formats the firmware version string used in HA `device.sw_version`.
    static void getSwVersionStr(char (&buf)[swVersionBufSize]);

    char deviceName[deviceNameBufSize]{};  // Human-readable device name, built in buildDeviceName().
    Connectivity& conn;
  };

private:
  static constexpr uint32_t deviceResetTime = Time::hrToMs(3U);     // Time before the device resets due to being offline.
  static constexpr uint32_t reconnectTime = Time::secToMs(10U);     // Time interval for retrying MQTT reconnections.
  static constexpr uint8_t dateTimeStrBufSize = 24U;                // Buffer size for ISO8601 date-time strings.
  static constexpr uint8_t macHexLen = 12U;                                                               // MAC address formatted as 6 hex byte pairs.
  static constexpr const char PROGMEM mqttClientName[]  = "%s_%s";                                        // MQTT client name: <deviceId>_<MAC>.
  static constexpr const char PROGMEM mqttOutTopic[]    = "iot/dtos/%s/";                                 // MQTT sender topic base: iot/dtos/<MAC>/.
  static constexpr const char PROGMEM mqttInTopic[]     = "iot/stod/%s/#";                                // MQTT receiver topic: iot/stod/<MAC>/#.
  static constexpr const char PROGMEM mqttAvailTopic[]  = "%savailability";                               // MQTT availability topic suffix; %s receives senderTopic ("iot/dtos/<MAC>/").
  static constexpr const char PROGMEM mqttInfoTopic[]   = "%sinfo";                                       // MQTT device info topic: iot/dtos/<MAC>/info.
  static constexpr const char PROGMEM mqttInfoPayload[] = R"({"fw":%hu,"git":"%x","dirty":%hu,"rr":%hu})"; // Device info JSON payload.
  // Sizes derived from the format strings: sizeof includes null; %s (2 chars) is replaced by macHexLen chars.
  static constexpr uint8_t senderTopicBufSize   = sizeof(mqttOutTopic)  - 2U + macHexLen;                 // "iot/dtos/<MAC>/" + null.
  static constexpr uint8_t receiverTopicBufSize = sizeof(mqttInTopic)   - 2U + macHexLen;                 // "iot/stod/<MAC>/#" + null.
  static constexpr uint8_t subtopicOffset       = sizeof(mqttInTopic)   - 4U + macHexLen;                 // sizeof - null - '#' - "%s"(2) + macHexLen.
  static constexpr uint8_t availTopicBufSize    = sizeof(mqttAvailTopic) - 2U + senderTopicBufSize - 1U;  // "iot/dtos/<MAC>/availability" + null.
  static constexpr uint8_t infoTopicBufSize     = sizeof(mqttInfoTopic)  - 2U + senderTopicBufSize - 1U;  // "iot/dtos/<MAC>/info" + null.
  static constexpr uint8_t infoPayloadBufSize   = 52U;                                                    // {"fw":65535,"git":"ffffffff","dirty":1,"rr":255} = 48 chars + null.

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
  static constexpr const char availOnlinePayload[]  = R"({"state":"online"})";                            // Availability online payload (RAM; publish/connect require a normal pointer).
  static constexpr const char availOfflinePayload[] = R"({"state":"offline"})";                           // Availability offline payload (RAM; publish/connect require a normal pointer).

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
    char senderTopic[senderTopicBufSize]{};                         // MQTT base topic for outgoing messages.
    char receiverTopic[receiverTopicBufSize]{};                     // MQTT topic for incoming messages.
    char availabilityTopic[availTopicBufSize]{};                    // MQTT availability topic for online/offline signalling.

    /// @brief Initializes all members to default values.
    MqttCredentials() = default;
  };

  /// @brief Establishes a connection to the MQTT broker.
  /// @return `true` if the connection was successfully established; otherwise, `false`.
  bool connectToMqttServer();

  /// @brief Synchronizes the system time using NTP.
  /// @return `true` if synchronisation completed within the timeout; `false` if it timed out.
  [[nodiscard]] static bool syncNtpTime();

  /// @brief Retrieves the current time as an ISO8601 string.
  /// @param dateTimeBuffer Buffer to store the ISO8601 string.
  /// @return `true` if the time is retrieved successfully; otherwise, `false`.
  [[nodiscard]] static bool getIsoTimeString(char (&dateTimeBuffer)[dateTimeStrBufSize]);

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
    const int32_t responseBufferActualSize = snprintf_P(responseBuffer, sizeof(responseBuffer),
      PSTR(R"({"type":%hu,"cmd":%hu,"err":%u})"), static_cast<uint8_t>(response), command, errCode);
    const bool responseBufferValid = ((responseBufferActualSize >= 0) &&
      (responseBufferActualSize < static_cast<int32_t>(sizeof(responseBuffer))));
    if(!responseBufferValid) { return false; }
    return sendMessage(responseBuffer);
  }

  /// @brief Called on every MQTT connect to publish the HA discovery config for this entity.
  /// Override in derived classes that expose HA entities; the default no-op is safe for handlers
  /// that do not need HA discovery (e.g. file-transfer handlers).
  /// @return `true` if publishing succeeded or no discovery is needed; `false` on publish failure.
  virtual bool publishDiscovery() { return true; }

  /// @brief Publishes the HA MQTT discovery config for this entity via `HADiscovery::publishEntity`.
  /// Call from a `publishDiscovery()` override with a brace-initialized `EntityConfig`.
  /// @param config Discovery config — both PROGMEM pointers must be
  ///        `static constexpr const char PROGMEM` members of the derived class.
  /// @return `true` if the message was sent successfully; otherwise, `false`.
  [[nodiscard]] bool doPublishEntityDiscovery(const Connectivity::HADiscovery::EntityConfig& config) {
    return connectivity.publishEntityDiscovery(subtopic, config);
  }

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
    connectivity(connectivity)
  {
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