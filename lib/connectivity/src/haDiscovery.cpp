#include "haDiscovery.hpp"
#include "common.hpp"                                               /// Common definitions and functions.
#include <ctype.h>

HADiscovery::HADiscovery(PubSubClient& mqttClient,
                         const char* clientName,
                         const char* senderTopic,
                         const char* receiverTopic,
                         const char* availabilityTopic) :
  mqttClient(mqttClient),
  clientName(clientName),
  senderTopic(senderTopic),
  receiverTopic(receiverTopic),
  availabilityTopic(availabilityTopic)
{}

void HADiscovery::getSwVersionStr(char (&buf)[swVersionBufSize]) {
  (void)snprintf_P(buf, sizeof(buf), PSTR("%hu (%08x)"), Build::getFwVersion(), Build::getGitHash());
}

void HADiscovery::buildDeviceName(const uint8_t mac[6], const char* deviceId) {
  memset(deviceName, '\0', sizeof(deviceName));
  for(uint8_t i = 0U; i < (deviceNameBufSize - 8U) && deviceId[i] != '\0'; ++i) {
    deviceName[i] = (deviceId[i] == '_')
      ? ' ' : static_cast<char>(toupper(static_cast<unsigned char>(deviceId[i])));
  }
  const uint8_t prefixLen = static_cast<uint8_t>(strnlen(deviceName, deviceNameBufSize));
  deviceName[prefixLen] = ' ';
  (void)snprintf_P(deviceName + prefixLen + 1U, 7U, PSTR("%02X%02X%02X"), mac[3], mac[4], mac[5]);
  Logger::get().printf_P(PSTR("[HA] Device name: %s\r\n"), deviceName);
}

HADiscovery::EntityConfig
HADiscovery::EntityConfig::sensor(
    const char* name, const char* valueTemplate, const char* unit,
    StateClass stateClass, DeviceClass deviceClass,
    const char* icon, const char* attributesTemplate) {
  EntityConfig c;
  c.type               = EntityType::sensor;
  c.name               = name;
  c.valueTemplate      = valueTemplate;
  c.unit               = unit;
  c.stateClass         = stateClass;
  c.deviceClass        = deviceClass;
  c.icon               = icon;
  c.attributesTemplate = attributesTemplate;
  return c;
}

HADiscovery::EntityConfig
HADiscovery::EntityConfig::button(
    const char* name, const char* cmdValue, DeviceClass deviceClass) {
  EntityConfig c;
  c.type           = EntityType::button;
  c.name           = name;
  c.payloadPress   = cmdValue;
  c.deviceClass    = deviceClass;
  c.isCommandTopic = true;
  return c;
}

HADiscovery::EntityConfig
HADiscovery::EntityConfig::binarySensor(
    const char* name, const char* valueTemplate,
    const char* payloadOn, const char* payloadOff,
    DeviceClass deviceClass, const char* icon) {
  EntityConfig c;
  c.type          = EntityType::binary_sensor;
  c.name          = name;
  c.valueTemplate = valueTemplate;
  c.payloadOn     = payloadOn;
  c.payloadOff    = payloadOff;
  c.deviceClass   = deviceClass;
  c.icon          = icon;
  return c;
}

bool HADiscovery::publishConnectivity() { // NOLINT(readability-convert-member-functions-to-static)
  const EntityConfig config = EntityConfig::binarySensor(
    PSTR("Connection"), PSTR("{{ value_json.state }}"),
    PSTR("online"), PSTR("offline"), DeviceClass::connectivity);
  // "availability" is the literal suffix of the availability topic (after the sender topic base).
  const bool result = publishEntity(PSTR("availability"), config);
  Logger::get().printf_P(PSTR("[HA] Connection discovery: %s\r\n"), Str::getStateStr(result));
  return result;
}

bool HADiscovery::publishEntity(const char* subtopic, const EntityConfig& config) {
  const char* haType = getTypeStr(config.type);
  if(subtopic == nullptr || haType == nullptr || config.name == nullptr) { return false; }
  char swVersion[swVersionBufSize] = { '\0' };
  getSwVersionStr(swVersion);
  char discTopic[discoveryTopicBufSize] = { '\0' };
  {
    const int32_t n = snprintf_P(discTopic, sizeof(discTopic), mqttDiscoveryTopic,
      haType, clientName, subtopic);
    if(n < 0 || n >= static_cast<int32_t>(sizeof(discTopic))) { return false; }
  }
  // Build topic base: senderTopic for state_topic, receiverTopic (trimmed) for command_topic.
  char topicBase[MqttTopics::getReceiverTopicBufSize()] = { '\0' };
  if(config.isCommandTopic) {
    strlcpy(topicBase, receiverTopic, MqttTopics::getSubtopicOffset() + 1U);
  } else {
    strlcpy(topicBase, senderTopic, sizeof(topicBase));
  }
  const char* topicField = config.isCommandTopic ? topicFieldCmd : topicFieldState;

  // Build payload incrementally — only set fields appear in the JSON output.
  char payload[discoveryPayloadBufSize] = { '\0' };
  size_t pos = 0U;
  int32_t n = 0;

#define PAYLOAD_APPEND(fmt, ...) \
  n = snprintf_P(payload + pos, sizeof(payload) - pos, PSTR(fmt), ##__VA_ARGS__); \
  if(n < 0 || static_cast<size_t>(n) >= sizeof(payload) - pos) { return false; } \
  pos += static_cast<size_t>(n);

  PAYLOAD_APPEND(R"({"unique_id":"%s_%s","name":"%s")", clientName, subtopic, config.name)
  if(config.valueTemplate      != nullptr) { PAYLOAD_APPEND(R"(,"value_template":"%s")",           config.valueTemplate) }
  if(config.payloadOn          != nullptr) { PAYLOAD_APPEND(R"(,"payload_on":"%s")",               config.payloadOn) }
  if(config.payloadOff         != nullptr) { PAYLOAD_APPEND(R"(,"payload_off":"%s")",              config.payloadOff) }
  if(config.payloadPress       != nullptr) { PAYLOAD_APPEND(R"(,"payload_press":"{\"cmd\":\"%s\"}")", config.payloadPress) }
  if(config.unit               != nullptr) { PAYLOAD_APPEND(R"(,"unit_of_measurement":"%s")",      config.unit) }
  {
    const char* sc = getStateClassStr(config.stateClass);
    if(sc != nullptr)                      { PAYLOAD_APPEND(R"(,"state_class":"%s")",              sc) }
  }
  {
    const char* dc = getDeviceClassStr(config.deviceClass);
    if(dc != nullptr)                      { PAYLOAD_APPEND(R"(,"device_class":"%s")",             dc) }
  }
  if(config.icon               != nullptr) { PAYLOAD_APPEND(R"(,"icon":"%s")",                     config.icon) }
  if(config.attributesTemplate != nullptr) { PAYLOAD_APPEND(R"(,"json_attributes_template":"%s")", config.attributesTemplate) }
  PAYLOAD_APPEND(R"(,"%s":"%s%s")", topicField, topicBase, subtopic)
  if(!config.isCommandTopic)               { PAYLOAD_APPEND(R"(,"json_attributes_topic":"%s%s")",  topicBase, subtopic) }
  PAYLOAD_APPEND(R"(,"availability":[{"topic":"%s","value_template":"{{ value_json.state }}"}])", availabilityTopic)
  PAYLOAD_APPEND(R"(,"device":{"identifiers":["%s"],"name":"%s","sw_version":"%s"}})", clientName, deviceName, swVersion)

#undef PAYLOAD_APPEND

  return mqttClient.publish(discTopic, payload, true);
}
