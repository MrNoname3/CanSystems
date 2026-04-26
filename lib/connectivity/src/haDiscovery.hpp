#ifndef HADISCOVERY_HPP
#define HADISCOVERY_HPP

#include <stdint.h>
#include <pgmspace.h>
#include <PubSubClient.h>
#include "mqttTopics.hpp"

/// @brief Handles Home Assistant MQTT auto-discovery.
/// All HA-specific strings, enums, and publish logic live here; handlers provide only typed data.
class HADiscovery {
public:
  /// @brief Supported Home Assistant MQTT component types.
  enum class EntityType  : uint8_t { sensor, binary_sensor, button };
  /// @brief Supported HA `state_class` values.
  enum class StateClass  : uint8_t { none, measurement, total_increasing };
  /// @brief Supported HA `device_class` values.
  enum class DeviceClass : uint8_t { none, connectivity, restart };

  /// @brief Typed discovery configuration for a single entity.
  /// Construct via the factory methods: `EntityConfig::sensor()`, `::button()`, `::binarySensor()`.
  struct EntityConfig {
    EntityType   type               = EntityType::sensor;
    const char*  name               = nullptr;  // PROGMEM: human-readable entity name.
    const char*  valueTemplate      = nullptr;  // PROGMEM: Jinja2 template to extract state value.
    const char*  payloadPress       = nullptr;  // PROGMEM: command name for button (e.g. "reboot"); HADiscovery wraps it as {"cmd":"<value>"}.
    const char*  payloadOn          = nullptr;  // PROGMEM: binary_sensor on-payload string.
    const char*  payloadOff         = nullptr;  // PROGMEM: binary_sensor off-payload string.
    const char*  unit               = nullptr;  // PROGMEM: unit of measurement (e.g. "CPM").
    StateClass   stateClass         = StateClass::none;
    DeviceClass  deviceClass        = DeviceClass::none;
    const char*  icon               = nullptr;  // PROGMEM: Material Design icon (e.g. "mdi:remote").
    const char*  attributesTemplate = nullptr;  // PROGMEM: Jinja2 template for JSON attributes.
    bool         isCommandTopic     = false;    // true → command_topic; false → state_topic.

    /// @brief Creates config for a sensor entity.
    static EntityConfig sensor(const char* name, const char* valueTemplate,
                               const char* unit = nullptr,
                               StateClass stateClass = StateClass::none,
                               DeviceClass deviceClass = DeviceClass::none,
                               const char* icon = nullptr,
                               const char* attributesTemplate = nullptr);

    /// @brief Creates config for a button entity. HADiscovery wraps `cmdValue` as `{"cmd":"<cmdValue>"}`.
    static EntityConfig button(const char* name, const char* cmdValue,
                               DeviceClass deviceClass = DeviceClass::none);

    /// @brief Creates config for a binary_sensor entity.
    static EntityConfig binarySensor(const char* name, const char* valueTemplate,
                                     const char* payloadOn, const char* payloadOff,
                                     DeviceClass deviceClass = DeviceClass::none,
                                     const char* icon = nullptr);
  };

  /// @brief Constructs the HADiscovery instance with references to the MQTT client and topic strings.
  /// The topic string pointers must remain valid for the lifetime of this object and will be
  /// read-only after construction; they are populated by Connectivity before first publish.
  /// @param mqttClient Reference to the MQTT client used for publishing discovery messages.
  /// @param clientName Pointer to the MQTT client name buffer.
  /// @param senderTopic Pointer to the MQTT sender topic buffer.
  /// @param receiverTopic Pointer to the MQTT receiver topic buffer.
  /// @param availabilityTopic Pointer to the MQTT availability topic buffer.
  HADiscovery(PubSubClient& mqttClient,
              const char* clientName,
              const char* senderTopic,
              const char* receiverTopic,
              const char* availabilityTopic);
  HADiscovery(const HADiscovery&)            = delete;
  HADiscovery& operator=(const HADiscovery&) = delete;

  /// @brief Builds the human-readable device name from the deviceId and MAC address.
  void buildDeviceName(const uint8_t mac[6], const char* deviceId);

  /// @brief Publishes the HA MQTT discovery config for any entity type.
  /// Assembles the full JSON payload from the typed config fields; only fields that are set
  /// appear in the payload. `json_attributes_topic` is added automatically for state-topic entities.
  /// @param subtopic Entity subtopic — used to build unique_id and complete the topic URL.
  /// @param config Typed entity discovery configuration.
  /// @return `true` if the discovery message was published successfully; otherwise, `false`.
  [[nodiscard]] bool publishEntity(const char* subtopic, const EntityConfig& config);

  /// @brief Publishes the HA MQTT discovery config for the built-in connectivity binary sensor.
  [[nodiscard]] bool publishConnectivity();

private:
  static constexpr uint8_t  discoveryTopicBufSize   = 96U;   // "homeassistant/<type>/<uid>/config" topic buffer.
  static constexpr uint16_t discoveryPayloadBufSize = 640U;  // HA MQTT discovery JSON payload buffer.
  static constexpr uint8_t  swVersionBufSize        = 24U;   // "65535 (ffffffff)" sw version string buffer.
  static constexpr uint8_t  deviceNameBufSize       = 32U;   // "ESP32 CAN A1B2C3" device name buffer.

  // HA component type strings (PROGMEM).
  static constexpr const char PROGMEM typeStrSensor[]          = "sensor";
  static constexpr const char PROGMEM typeStrBinarySensor[]    = "binary_sensor";
  static constexpr const char PROGMEM typeStrButton[]          = "button";
  // HA state_class strings (PROGMEM).
  static constexpr const char PROGMEM stateClassMeasurement[]  = "measurement";
  static constexpr const char PROGMEM stateClassTotalIncr[]    = "total_increasing";
  // HA device_class strings (PROGMEM).
  static constexpr const char PROGMEM deviceClassConn[]        = "connectivity";
  static constexpr const char PROGMEM deviceClassRestart[]     = "restart";
  // HA topic field name strings (PROGMEM).
  static constexpr const char PROGMEM topicFieldState[]        = "state_topic";
  static constexpr const char PROGMEM topicFieldCmd[]          = "command_topic";
  // HA discovery topic format string (PROGMEM).
  static constexpr const char PROGMEM mqttDiscoveryTopic[]     = "homeassistant/%s/%s_%s/config";

  static constexpr const char* getTypeStr(EntityType t) {
    switch(t) {
      case EntityType::sensor:        return typeStrSensor;
      case EntityType::binary_sensor: return typeStrBinarySensor;
      case EntityType::button:        return typeStrButton;
      default:                        return nullptr;
    }
  }
  static constexpr const char* getStateClassStr(StateClass sc) {
    switch(sc) {
      case StateClass::measurement:      return stateClassMeasurement;
      case StateClass::total_increasing: return stateClassTotalIncr;
      default:                           return nullptr;
    }
  }
  static constexpr const char* getDeviceClassStr(DeviceClass dc) {
    switch(dc) {
      case DeviceClass::connectivity: return deviceClassConn;
      case DeviceClass::restart:      return deviceClassRestart;
      default:                        return nullptr;
    }
  }

  /// @brief Formats the firmware version string used in HA `device.sw_version`.
  static void getSwVersionStr(char (&buf)[swVersionBufSize]);

  char         deviceName[deviceNameBufSize]{};
  PubSubClient& mqttClient;
  const char*  clientName;
  const char*  senderTopic;
  const char*  receiverTopic;
  const char*  availabilityTopic;
};
#endif // HADISCOVERY_HPP
