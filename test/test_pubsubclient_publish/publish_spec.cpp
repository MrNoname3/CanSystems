#include "PubSubClient.h"
#include "ShimClient.h"
#include "Buffer.h"
#include "BDDTest.h"
#include "trace.h"

uint8_t server[] = {172U, 16U, 0U, 2U};

void callback(char* topic, uint8_t* payload, unsigned int length) {
  // handle message arrived
}

bool test_publish() {
  IT("publishes a null-terminated string");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = {0x20U, 0x02U, 0x00U, 0x00U};
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = {0x30U, 0xeU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U};
  shimClient.expect(publish, 16U);

  rc = client.publish("topic", "payload");
  IS_TRUE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_publish_bytes() {
  IT("publishes a uint8_t array");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t payload[] = {0x01U, 0x02U, 0x03U, 0x0U, 0x05U};
  uint8_t length = 5U;

  uint8_t connack[] = {0x20U, 0x02U, 0x00U, 0x00U};
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = {0x30U, 0xcU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x1U, 0x2U, 0x3U, 0x0U, 0x5U};
  shimClient.expect(publish, 14U);

  rc = client.publish("topic", payload, length);
  IS_TRUE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_publish_retained() {
  IT("publishes retained - 1");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t payload[] = {0x01U, 0x02U, 0x03U, 0x0U, 0x05U};
  uint8_t length = 5U;

  uint8_t connack[] = {0x20U, 0x02U, 0x00U, 0x00U};
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = {0x31U, 0xcU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x1U, 0x2U, 0x3U, 0x0U, 0x5U};
  shimClient.expect(publish, 14U);

  rc = client.publish("topic", payload, length, true);
  IS_TRUE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_publish_retained_2() {
  IT("publishes retained - 2");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = {0x20U, 0x02U, 0x00U, 0x00U};
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = {0x31U, 0xcU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 'A', 'B', 'C', 'D', 'E'};
  shimClient.expect(publish, 14U);

  rc = client.publish("topic", "ABCDE", true);
  IS_TRUE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_publish_not_connected() {
  IT("publish fails when not connected");
  ShimClient shimClient;

  PubSubClient client(server, 1883U, callback, shimClient);

  bool rc = client.publish("topic", "payload");
  IS_FALSE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_publish_too_long() {
  IT("publish fails when topic/payload are too long");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = {0x20U, 0x02U, 0x00U, 0x00U};
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  client.setBufferSize(128U);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  //                                         0        1         2         3         4         5         6         7         8         9         0         1         2
  rc = client.publish("topic", "123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890");
  IS_FALSE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_publish_P() {
  IT("publishes using PROGMEM");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t payload[] = {0x01U, 0x02U, 0x03U, 0x0U, 0x05U};
  uint8_t length = 5U;

  uint8_t connack[] = {0x20U, 0x02U, 0x00U, 0x00U};
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = {0x31U, 0xcU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x1U, 0x2U, 0x3U, 0x0U, 0x5U};
  shimClient.expect(publish, 14U);

  rc = client.publish_P("topic", payload, length, true);
  IS_TRUE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

int main() {
  SUITE("Publish");
  test_publish();
  test_publish_bytes();
  test_publish_retained();
  test_publish_retained_2();
  test_publish_not_connected();
  test_publish_too_long();
  test_publish_P();

  FINISH
}
