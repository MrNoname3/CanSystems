#ifndef MQTTTOPICS_HPP
#define MQTTTOPICS_HPP

#include <stdint.h>
#include <pgmspace.h>

/// @brief MQTT topic format strings and derived buffer sizes, shared between Connectivity and HADiscovery.
class MqttTopics {
private:
  static constexpr uint8_t macHexLen = 12U;                                                               // MAC address formatted as 6 hex byte pairs.
  static constexpr const char PROGMEM mqttClientName[]  = "%s_%s";                                        // MQTT client name: <deviceId>_<MAC>.
  static constexpr const char PROGMEM mqttOutTopic[]    = "iot/dtos/%s/";                                 // MQTT sender topic base: iot/dtos/<MAC>/.
  static constexpr const char PROGMEM mqttInTopic[]     = "iot/stod/%s/#";                                // MQTT receiver topic: iot/stod/<MAC>/#.
  static constexpr const char PROGMEM mqttAvailTopic[]  = "%savailability";                               // MQTT availability topic suffix; %s receives senderTopic ("iot/dtos/<MAC>/").
  // Sizes derived from the format strings: sizeof includes null; %s (2 chars) is replaced by macHexLen chars.
  static constexpr uint8_t senderTopicBufSize   = sizeof(mqttOutTopic)   - 2U + macHexLen;               // "iot/dtos/<MAC>/" + null.
  static constexpr uint8_t receiverTopicBufSize = sizeof(mqttInTopic)    - 2U + macHexLen;               // "iot/stod/<MAC>/#" + null.
  static constexpr uint8_t subtopicOffset       = sizeof(mqttInTopic)    - 4U + macHexLen;               // sizeof - null - '#' - "%s"(2) + macHexLen.
  static constexpr uint8_t availTopicBufSize    = sizeof(mqttAvailTopic) - 2U + senderTopicBufSize - 1U; // "iot/dtos/<MAC>/availability" + null.

public:
  static constexpr uint8_t       getMacHexLen()           { return macHexLen; }
  static constexpr const char*   getMqttClientName()      { return mqttClientName; }
  static constexpr const char*   getMqttOutTopic()        { return mqttOutTopic; }
  static constexpr const char*   getMqttInTopic()         { return mqttInTopic; }
  static constexpr const char*   getMqttAvailTopic()      { return mqttAvailTopic; }
  static constexpr uint8_t       getSenderTopicBufSize()  { return senderTopicBufSize; }
  static constexpr uint8_t       getReceiverTopicBufSize(){ return receiverTopicBufSize; }
  static constexpr uint8_t       getSubtopicOffset()      { return subtopicOffset; }
  static constexpr uint8_t       getAvailTopicBufSize()   { return availTopicBufSize; }
};
#endif // MQTTTOPICS_HPP
