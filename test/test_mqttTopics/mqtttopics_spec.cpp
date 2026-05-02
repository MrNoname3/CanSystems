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
  const int n = snprintf(buf, sizeof(buf), MqttTopics::getMqttInfoPayload(),
    static_cast<uint16_t>(65535U), 0xFFFFFFFFU,
    static_cast<uint16_t>(255U), static_cast<uint16_t>(255U));
  TEST(n > 0 && static_cast<size_t>(n) < sizeof(buf));
  END_IT
}

bool test_availPayloads() {
  IT("availability payloads have the correct content");
  IS_EQUAL(strcmp(MqttTopics::availOnlinePayload,  R"({"state":"online"})"),  0);
  IS_EQUAL(strcmp(MqttTopics::availOfflinePayload, R"({"state":"offline"})"), 0);
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
  test_availPayloads();
  FINISH
}
