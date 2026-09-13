#include "PubSubClient.h"
#include "ShimClient.h"
#include "Buffer.h"
#include "BDDTest.h"
#include "trace.h"
#include <string.h>

uint8_t server[] = { 172U, 16U, 0U, 2U };

bool callback_called = false;
char lastTopic[1024];
char lastPayload[1024];
uint32_t lastLength = 0U;

void reset_callback() {
  callback_called = false;
  lastTopic[0] = '\0';
  lastPayload[0] = '\0';
  lastLength = 0U;
}

void callback(char* topic, uint8_t* payload, unsigned int length) {
  callback_called = true;
  strcpy(lastTopic, topic);
  memcpy(lastPayload, payload, static_cast<size_t>(length));   // NOLINT(bugprone-narrowing-conversions)
  lastLength = length;
}

bool test_publish() {
  IT("publishes a null-terminated string");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x30U, 0xeU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
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

  const uint8_t payload[] = { 0x01U, 0x02U, 0x03U, 0x0U, 0x05U };
  uint8_t length = 5U;

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x30U, 0xcU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x1U, 0x2U, 0x3U, 0x0U, 0x5U };
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

  const uint8_t payload[] = { 0x01U, 0x02U, 0x03U, 0x0U, 0x05U };
  uint8_t length = 5U;

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x31U, 0xcU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x1U, 0x2U, 0x3U, 0x0U, 0x5U };
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

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x31U, 0xcU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 'A', 'B', 'C', 'D', 'E' };
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

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.setBufferSize(128U));
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

  const uint8_t payload[] = { 0x01U, 0x02U, 0x03U, 0x0U, 0x05U };
  uint8_t length = 5U;

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x31U, 0xcU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x1U, 0x2U, 0x3U, 0x0U, 0x5U };
  shimClient.expect(publish, 14U);

  rc = client.publish_P("topic", payload, length, true);
  IS_TRUE(rc);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_publish_half_written_ends_the_session() {
  IT("a publish the link took only part of ends the session");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  shimClient.truncateNextWrite(5U);
  rc = client.publish("topic", "payload");
  IS_FALSE(rc);
  // The broker is now half way through a PUBLISH that never ends; nothing more can go down it.
  IS_FALSE(client.connected());
  IS_TRUE(client.state() == PubSubClient::State::CONNECTION_LOST);

  END_IT
}

bool test_publish_refused_keeps_the_session() {
  IT("a publish the link would not take at all leaves the session up");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  // Nothing went out, so the stream is where it was and the caller can simply try again.
  shimClient.failNextWrites(1U);
  rc = client.publish("topic", "payload");
  IS_FALSE(rc);
  IS_TRUE(client.connected());

  END_IT
}

bool test_publish_P_too_long() {
  IT("publishes from PROGMEM only what the buffer can frame");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.setBufferSize(128U));
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t payload[] = { 0x41U };
  // Only the header and the topic are built in the buffer, so 121 characters still fit it.
  static char topic[128];
  memset(topic, 'a', 121U);
  topic[121] = '\0';
  rc = client.publish_P(topic, payload, 1U, false);
  IS_TRUE(rc);

  // One more, and the topic would be written past the end of the buffer. Nothing is expected from
  // here on, so a write of any kind is an error the shim records.
  shimClient.expect(nullptr, 0U);
  memset(topic, 'a', 122U);
  topic[122] = '\0';
  rc = client.publish_P(topic, payload, 1U, false);
  IS_FALSE(rc);
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_publish_without_a_topic() {
  IT("refuses a publish with no topic to send it to");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  // Nothing is expected from here on, so a write of any kind is an error the shim records.
  shimClient.expect(nullptr, 0U);
  // A subscribe says no to this; a publish went on to measure the string that is not there.
  rc = client.publish(nullptr, "payload");
  IS_FALSE(rc);
  const uint8_t payload[] = { 0x41U };
  rc = client.publish_P(nullptr, payload, 1U, false);
  IS_FALSE(rc);
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_publish_P_half_written_ends_the_session() {
  IT("a PROGMEM publish the link took only part of ends the session");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t payload[] = { 0x41U, 0x42U, 0x43U };
  shimClient.truncateNextWrite(3U);
  rc = client.publish_P("topic", payload, 3U, false);
  IS_FALSE(rc);
  IS_FALSE(client.connected());
  IS_TRUE(client.state() == PubSubClient::State::CONNECTION_LOST);

  END_IT
}

bool test_publish_refuses_a_topic_name_the_standard_forbids() {
  IT("refuses to publish to an empty topic or one carrying a wildcard");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  const uint16_t afterConnect = shimClient.received();

  // "The wildcard characters can be used in Topic Filters, but MUST NOT be used within a Topic
  // Name" [MQTT-4.7.1-1], and a topic name is at least one character long [MQTT-4.7.3-1].
  IS_FALSE(client.publish("home/+/temp", "1"));
  IS_FALSE(client.publish("home/#", "1"));
  IS_FALSE(client.publish("", "1"));
  IS_FALSE(client.publish_P("home/+/temp", "1", false));

  IS_EQUAL(shimClient.received(), afterConnect);
  IS_TRUE(client.connected());
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_publish_finishes_a_message_that_was_part_read() {
  IT("finishes a part-read message before building a packet over it");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  // Eight bytes of a fourteen-byte PUBLISH to "topic", payload "hello": the reader keeps its place.
  const uint8_t head[] = { 0x30U, 0x0CU, 0x00U, 0x05U, 0x74U, 0x6fU, 0x70U, 0x69U };
  shimClient.respond(head, 8U);
  IS_TRUE(client.loop());
  IS_FALSE(callback_called);

  // The rest of it lands, and the application publishes before the next pass reads it. The packet
  // built for that publish goes in the buffer holding the eight bytes already collected.
  const uint8_t tail[] = { 0x63U, 0x68U, 0x65U, 0x6cU, 0x6cU, 0x6fU };
  shimClient.respond(tail, 6U);

  const uint8_t expected[] = { 0x30U, 0xaU, 0x0U, 0x3U, 0x6fU, 0x75U, 0x74U, 0x78U, 0x79U, 0x7aU, 0x31U, 0x32U };
  shimClient.expect(expected, 12U);
  IS_TRUE(client.publish("out", "xyz12"));

  // Delivered whole, and by the publish that would otherwise have written over it.
  IS_TRUE(callback_called);
  IS_TRUE(strcmp(lastTopic, "topic") == 0);
  IS_TRUE(lastLength == 5U);
  IS_TRUE(memcmp(lastPayload, "hello", 5U) == 0);
  IS_TRUE(client.connected());
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_publish_gives_up_on_a_message_that_never_finishes() {
  IT("refuses to publish over a part-read message the peer never finishes");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  (void)client.setSocketTimeout(1U);                      // keep the read timeout out of the suite's runtime
  IS_TRUE(client.connect("client_test1"));

  const uint8_t head[] = { 0x30U, 0x0CU, 0x00U, 0x05U, 0x74U, 0x6fU, 0x70U, 0x69U };
  shimClient.respond(head, 8U);
  IS_TRUE(client.loop());

  // Nothing behind it. Writing the publish anyway would leave the reader appending the rest of a
  // message onto bytes that are no longer its own.
  IS_FALSE(client.publish("out", "xyz"));
  IS_TRUE(client.state() == PubSubClient::State::CONNECTION_TIMEOUT);
  IS_FALSE(client.connected());
  IS_FALSE(callback_called);

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
  test_publish_P_too_long();
  test_publish_without_a_topic();
  test_publish_refuses_a_topic_name_the_standard_forbids();
  test_publish_finishes_a_message_that_was_part_read();
  test_publish_gives_up_on_a_message_that_never_finishes();
  test_publish_P_half_written_ends_the_session();
  test_publish_half_written_ends_the_session();
  test_publish_refused_keeps_the_session();

  FINISH
}
