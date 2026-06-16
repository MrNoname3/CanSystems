#include "PubSubClient.h"
#include "ShimClient.h"
#include "Buffer.h"
#include "BDDTest.h"
#include "trace.h"

uint8_t server[] = { 172U, 16U, 0U, 2U };

void callback([[maybe_unused]] char* topic, [[maybe_unused]] uint8_t* payload, [[maybe_unused]] unsigned int length) {
  // handle message arrived
}

bool test_subscribe_no_qos() {
  IT("subscribe without qos defaults to 0");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t subscribe[] = { 0x82U, 0xaU, 0x0U, 0x2U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x0U };
  shimClient.expect(subscribe, 12U);
  const uint8_t suback[] = { 0x90U, 0x3U, 0x0U, 0x2U, 0x0U };
  shimClient.respond(suback, 5U);

  rc = client.subscribe("topic");
  IS_TRUE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_subscribe_qos_1() {
  IT("subscribes qos 1");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t subscribe[] = { 0x82U, 0xaU, 0x0U, 0x2U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x1U };
  shimClient.expect(subscribe, 12U);
  const uint8_t suback[] = { 0x90U, 0x3U, 0x0U, 0x2U, 0x1U };
  shimClient.respond(suback, 5U);

  rc = client.subscribe("topic", 1U);
  IS_TRUE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_subscribe_not_connected() {
  IT("subscribe fails when not connected");
  ShimClient shimClient;

  PubSubClient client(server, 1883, callback, shimClient);

  bool rc = client.subscribe("topic");
  IS_FALSE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_subscribe_invalid_qos() {
  IT("subscribe fails with invalid qos values");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  rc = client.subscribe("topic", 2U);
  IS_FALSE(rc);
  rc = client.subscribe("topic", 254U);
  IS_FALSE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_subscribe_too_long() {
  IT("subscribe fails with too long topic");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.setBufferSize(128U));
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  // max length should be allowed
  //                            0        1         2         3         4         5         6         7         8         9         0         1         2
  rc = client.subscribe("12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789");
  IS_TRUE(rc);

  //                            0        1         2         3         4         5         6         7         8         9         0         1         2
  rc = client.subscribe("123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890");
  IS_FALSE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_unsubscribe() {
  IT("unsubscribes");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t unsubscribe[] = { 0xA2U, 0x9U, 0x0U, 0x2U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U };
  shimClient.expect(unsubscribe, 11U);
  const uint8_t unsuback[] = { 0xB0U, 0x2U, 0x0U, 0x2U };
  shimClient.respond(unsuback, 4U);

  rc = client.unsubscribe("topic");
  IS_TRUE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_unsubscribe_not_connected() {
  IT("unsubscribe fails when not connected");
  ShimClient shimClient;

  PubSubClient client(server, 1883U, callback, shimClient);

  bool rc = client.unsubscribe("topic");
  IS_FALSE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

int main() {
  SUITE("Subscribe");
  test_subscribe_no_qos();
  test_subscribe_qos_1();
  test_subscribe_not_connected();
  test_subscribe_invalid_qos();
  test_subscribe_too_long();
  test_unsubscribe();
  test_unsubscribe_not_connected();
  FINISH
}
