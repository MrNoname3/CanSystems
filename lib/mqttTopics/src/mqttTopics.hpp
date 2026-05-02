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
  static constexpr const char PROGMEM mqttAvailTopic[]  = "%savailability";                               // MQTT availability topic; %s receives the topic base ending with '/'.
  static constexpr const char PROGMEM mqttInfoTopic[]   = "%sinfo";                                       // MQTT retained device info topic; %s receives the topic base ending with '/'.
  static constexpr const char PROGMEM mqttInfoPayload[] = R"({"fw":%hu,"git":"%x","dirty":%hu,"rr":%hu})"; // Device info JSON payload; args: fwVersion, gitHash, gitDirty, resetReason.
  // Sizes derived from the format strings: sizeof includes null; %s (2 chars) is replaced by the base length.
  static constexpr uint8_t senderTopicBufSize   = sizeof(mqttOutTopic)   - 2U + macHexLen;               // "iot/dtos/<MAC>/" + null.
  static constexpr uint8_t receiverTopicBufSize = sizeof(mqttInTopic)    - 2U + macHexLen;               // "iot/stod/<MAC>/#" + null.
  static constexpr uint8_t subtopicOffset       = sizeof(mqttInTopic)    - 4U + macHexLen;               // sizeof - null - '#' - "%s"(2) + macHexLen.
  static constexpr uint8_t availTopicBufSize    = sizeof(mqttAvailTopic) - 2U + senderTopicBufSize - 1U; // "iot/dtos/<MAC>/availability" + null.
  static constexpr uint8_t infoTopicBufSize     = sizeof(mqttInfoTopic)  - 2U + senderTopicBufSize - 1U; // "iot/dtos/<MAC>/info" + null.
  static constexpr uint8_t infoPayloadBufSize   = 52U;                                                    // {"fw":65535,"git":"ffffffff","dirty":255,"rr":255} = 50 chars + null.

public:
  // Availability state payloads (RAM; PubSubClient::publish and connect require non-PROGMEM pointers).
  static constexpr const char availOnlinePayload[]  = R"({"state":"online"})";
  static constexpr const char availOfflinePayload[] = R"({"state":"offline"})";

  static constexpr uint8_t       getMacHexLen()            { return macHexLen; }
  static constexpr const char*   getMqttClientName()       { return mqttClientName; }
  static constexpr const char*   getMqttOutTopic()         { return mqttOutTopic; }
  static constexpr const char*   getMqttInTopic()          { return mqttInTopic; }
  static constexpr const char*   getMqttAvailTopic()       { return mqttAvailTopic; }
  static constexpr const char*   getMqttInfoTopic()        { return mqttInfoTopic; }
  static constexpr const char*   getMqttInfoPayload()      { return mqttInfoPayload; }
  static constexpr uint8_t       getSenderTopicBufSize()   { return senderTopicBufSize; }
  static constexpr uint8_t       getReceiverTopicBufSize() { return receiverTopicBufSize; }
  static constexpr uint8_t       getSubtopicOffset()       { return subtopicOffset; }
  static constexpr uint8_t       getAvailTopicBufSize()    { return availTopicBufSize; }
  static constexpr uint8_t       getInfoTopicBufSize()     { return infoTopicBufSize; }
  static constexpr uint8_t       getInfoPayloadBufSize()   { return infoPayloadBufSize; }
};
#endif // MQTTTOPICS_HPP
