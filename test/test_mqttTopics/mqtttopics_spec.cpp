#include "mqttTopics.hpp"
#include "BDDTest.h"
#include <stdio.h>
#include <string.h>

bool test_macHexLen() {
  IT("getMacHexLen returns 12");
  IS_EQUAL(MqttTopics::getMacHexLen(), 12U);
  END_IT
}

bool test_senderTopicBufSize() {
  IT("senderTopicBufSize fits the expanded sender topic");
  char buf[MqttTopics::getSenderTopicBufSize()];
  const int n = snprintf(buf, sizeof(buf), MqttTopics::getMqttOutTopic(), "AABBCCDDEEFF");
  TEST(n > 0 && static_cast<size_t>(n) < sizeof(buf));
  IS_EQUAL(strcmp(buf, "iot/dtos/AABBCCDDEEFF/"), 0);
  END_IT
}

bool test_receiverTopicBufSize() {
  IT("receiverTopicBufSize fits the expanded receiver topic");
  char buf[MqttTopics::getReceiverTopicBufSize()];
  const int n = snprintf(buf, sizeof(buf), MqttTopics::getMqttInTopic(), "AABBCCDDEEFF");
  TEST(n > 0 && static_cast<size_t>(n) < sizeof(buf));
  IS_EQUAL(strcmp(buf, "iot/stod/AABBCCDDEEFF/#"), 0);
  END_IT
}

bool test_availTopicBufSize() {
  IT("availTopicBufSize fits the full availability topic");
  char senderTopic[MqttTopics::getSenderTopicBufSize()];
  snprintf(senderTopic, sizeof(senderTopic), MqttTopics::getMqttOutTopic(), "AABBCCDDEEFF");
  char availTopic[MqttTopics::getAvailTopicBufSize()];
  const int n = snprintf(availTopic, sizeof(availTopic), MqttTopics::getMqttAvailTopic(), senderTopic);
  TEST(n > 0 && static_cast<size_t>(n) < sizeof(availTopic));
  IS_EQUAL(strcmp(availTopic, "iot/dtos/AABBCCDDEEFF/availability"), 0);
  END_IT
}

bool test_infoTopicBufSize() {
  IT("infoTopicBufSize fits the full info topic");
  char senderTopic[MqttTopics::getSenderTopicBufSize()];
  snprintf(senderTopic, sizeof(senderTopic), MqttTopics::getMqttOutTopic(), "AABBCCDDEEFF");
  char infoTopic[MqttTopics::getInfoTopicBufSize()];
  const int n = snprintf(infoTopic, sizeof(infoTopic), MqttTopics::getMqttInfoTopic(), senderTopic);
  TEST(n > 0 && static_cast<size_t>(n) < sizeof(infoTopic));
  IS_EQUAL(strcmp(infoTopic, "iot/dtos/AABBCCDDEEFF/info"), 0);
  END_IT
}

bool test_subtopicOffset() {
  IT("subtopicOffset points to '#' in the receiver topic");
  char receiverTopic[MqttTopics::getReceiverTopicBufSize()];
  snprintf(receiverTopic, sizeof(receiverTopic), MqttTopics::getMqttInTopic(), "AABBCCDDEEFF");
  // Trimming to subtopicOffset+1 must yield the base without '#' (used by HADiscovery for command_topic).
  IS_EQUAL(receiverTopic[MqttTopics::getSubtopicOffset()], '#');
  END_IT
}

bool test_infoPayloadBufSize() {
  IT("infoPayloadBufSize fits the maximum-length info payload");
  char buf[MqttTopics::getInfoPayloadBufSize()];
  const int n = snprintf(buf, sizeof(buf), MqttTopics::getMqttInfoPayload(), static_cast<uint16_t>(65535U), 0xFFFFFFFFU, static_cast<uint16_t>(255U), static_cast<uint16_t>(255U));
  TEST(n > 0 && static_cast<size_t>(n) < sizeof(buf));
  END_IT
}

bool test_nodeInfoPayloadBufSize() {
  IT("nodeInfoPayloadBufSize fits the maximum-length node info payload");
  char buf[MqttTopics::getNodeInfoPayloadBufSize()];
  const int n = snprintf(buf, sizeof(buf), MqttTopics::getMqttNodeInfoPayload(), static_cast<uint16_t>(65535U), 0xFFFFFFFFU, static_cast<uint16_t>(255U), static_cast<uint16_t>(255U), static_cast<uint8_t>(255U));
  TEST(n > 0 && static_cast<size_t>(n) < sizeof(buf));
  IS_EQUAL(strcmp(buf, R"({"fw":65535,"git":"ffffffff","dirty":255,"rr":255,"boot":255})"), 0);
  END_IT
}

bool test_diagPayloadBufSize() {
  IT("diagPayloadBufSize fits the maximum-length diagnostics payload");
  char buf[MqttTopics::getDiagPayloadBufSize()];
  // Longest cause string published by Connectivity plus maximal numeric fields.
  const int n = snprintf(buf, sizeof(buf), MqttTopics::getMqttDiagPayload(), "MQTT_CONNECT_BAD_CREDENTIALS", "2026-08-29T10:12:33Z", 4294967295U, 4294967295U);
  TEST(n > 0 && static_cast<size_t>(n) < sizeof(buf));
  IS_EQUAL(strcmp(buf, R"({"cause":"MQTT_CONNECT_BAD_CREDENTIALS","at":"2026-08-29T10:12:33Z","downSec":4294967295,"n":4294967295})"), 0);
  END_IT
}

bool test_availPayloads() {
  IT("availability payloads have the correct content");
  IS_EQUAL(strcmp(MqttTopics::getAvailOnlinePayload(), R"({"state":"online"})"), 0);
  IS_EQUAL(strcmp(MqttTopics::getAvailOfflinePayload(), R"({"state":"offline"})"), 0);
  END_IT
}

bool test_diagSubtopic() {
  IT("diagnostics subtopic has the correct content");
  IS_EQUAL(strcmp(MqttTopics::getDiagSubtopic(), "diag"), 0);
  END_IT
}

bool test_subtopicOf_splits_a_normal_topic() {
  IT("getSubtopicOf returns the part after \"iot/stod/<mac>/\"");
  IS_EQUAL(strcmp(MqttTopics::getSubtopicOf("iot/stod/aabbccddeeff/common"), "common"), 0);
  IS_EQUAL(strcmp(MqttTopics::getSubtopicOf("iot/stod/aabbccddeeff/alert1/ota"), "alert1/ota"), 0);
  END_IT
}

bool test_subtopicOf_rejects_the_parent_topic() {
  IT("getSubtopicOf returns nullptr for the parent topic that '#' also matches");
  // MQTT 3.1.1: "iot/stod/<mac>/#" matches "iot/stod/<mac>" as well, and that topic is one
  // character shorter than the offset - stepping over it would read past its terminator.
  IS_TRUE(MqttTopics::getSubtopicOf("iot/stod/aabbccddeeff") == nullptr);
  IS_TRUE(MqttTopics::getSubtopicOf("iot/stod/") == nullptr);
  IS_TRUE(MqttTopics::getSubtopicOf("") == nullptr);
  IS_TRUE(MqttTopics::getSubtopicOf(nullptr) == nullptr);
  END_IT
}

bool test_subtopicOf_accepts_an_empty_subtopic() {
  IT("getSubtopicOf returns the empty string for a topic that ends at the separator");
  // Long enough to index safely, so the pointer is handed back; the empty subtopic is then
  // rejected by MqttBase::isSubtopicValid rather than here.
  const char* sub = MqttTopics::getSubtopicOf("iot/stod/aabbccddeeff/");
  IS_TRUE(sub != nullptr);
  IS_EQUAL(strlen(sub), 0U);
  END_IT
}

int main() {
  SUITE("MqttTopics");
  test_macHexLen();
  test_senderTopicBufSize();
  test_receiverTopicBufSize();
  test_availTopicBufSize();
  test_infoTopicBufSize();
  test_subtopicOffset();
  test_infoPayloadBufSize();
  test_nodeInfoPayloadBufSize();
  test_diagPayloadBufSize();
  test_availPayloads();
  test_diagSubtopic();
  test_subtopicOf_splits_a_normal_topic();
  test_subtopicOf_rejects_the_parent_topic();
  test_subtopicOf_accepts_an_empty_subtopic();
  FINISH
}
