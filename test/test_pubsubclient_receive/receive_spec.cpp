#include "PubSubClient.h"
#include "ShimClient.h"
#include "Buffer.h"
#include "BDDTest.h"
#include "trace.h"
#include "Arduino.h"

uint8_t server[] = { 172U, 16U, 0U, 2U };

bool callback_called = false;
char lastTopic[1024];
char lastPayload[1024];
uint32_t lastLength;

void reset_callback() {
  callback_called = false;
  lastTopic[0] = '\0';
  lastPayload[0] = '\0';
  lastLength = 0U;
}

void callback(char* topic, uint8_t* payload, uint32_t length) {
  TRACE("Callback received topic=[" << topic << "] length=" << length << "\n")
  callback_called = true;
  strcpy(lastTopic, topic);
  memcpy(lastPayload, payload, static_cast<size_t>(length)); // NOLINT(bugprone-narrowing-conversions)
  lastLength = length;
}

bool test_receive_callback() {
  IT("receives a callback message");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x30U, 0xeU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  shimClient.respond(publish, 16U);

  rc = client.loop();

  IS_TRUE(rc);

  IS_TRUE(callback_called);
  IS_TRUE(strcmp(lastTopic, "topic") == 0);
  IS_TRUE(memcmp(lastPayload, "payload", 7U) == 0);
  IS_TRUE(lastLength == 7U);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_receive_max_sized_message() {
  IT("receives an max-sized message");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  uint8_t length = 80U;  // If this is changed to > 128 then the publish packet below
                         // is no longer valid as it assumes the remaining length
                         // is a single-uint8_t. Don't make that mistake like I just
                         // did and lose a whole evening tracking down the issue.
  IS_TRUE(client.setBufferSize(length));
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x30U, static_cast<uint8_t>(length - 2U), 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  uint8_t bigPublish[length + 1];                    // +1: bigPublish[length] holds a guard byte set below
  memset(bigPublish, 'A', length);
  bigPublish[length] = 'B';
  memcpy(bigPublish, publish, 16U);
  shimClient.respond(bigPublish, length);

  rc = client.loop();

  IS_TRUE(rc);

  IS_TRUE(callback_called);
  IS_TRUE(strcmp(lastTopic, "topic") == 0);
  IS_TRUE(lastLength == length - 9U);
  IS_TRUE(memcmp(lastPayload, bigPublish + 9U, lastLength) == 0);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_receive_oversized_message() {
  IT("ends the session on a message the buffer cannot hold");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  uint8_t length = 80U;  // See comment in test_receive_max_sized_message before changing this value

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.setBufferSize(static_cast<uint16_t>(length - 1U)));
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x30U, static_cast<uint8_t>(length - 2U), 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  uint8_t bigPublish[length + 1];                    // +1: bigPublish[length] holds a guard byte set below
  memset(bigPublish, 'A', length);
  bigPublish[length] = 'B';
  memcpy(bigPublish, publish, 16U);
  shimClient.respond(bigPublish, length);

  // An internal buffer full condition is a Transient Error, which [MQTT-4.8.0-2] answers by
  // closing the connection: a message read in part and passed on would be worse than none.
  rc = client.loop();

  IS_FALSE(rc);
  IS_TRUE(client.state() == PubSubClient::State::PACKET_TOO_LARGE);
  IS_FALSE(client.connected());
  IS_FALSE(callback_called);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_nothing_behind_an_oversized_message_is_read() {
  IT("reads nothing behind an oversized message, the session having ended with it");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  const uint8_t length = 80U;
  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.setBufferSize(static_cast<uint16_t>(length - 1U)));
  IS_TRUE(client.connect("client_test1"));

  const uint8_t publish[] = { 0x30U, static_cast<uint8_t>(length - 2U), 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  uint8_t bigPublish[length];
  memset(bigPublish, 'A', length);
  memcpy(bigPublish, publish, 16U);
  shimClient.respond(bigPublish, length);
  // Right behind it, a message that fits. It stays unread: the connection the two arrived on is
  // gone, and what is left in the stream goes with it.
  const uint8_t smallPublish[] = { 0x30U, 0xeU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  shimClient.respond(smallPublish, 16U);

  IS_FALSE(client.loop());
  IS_FALSE(callback_called);
  IS_FALSE(client.loop());
  IS_FALSE(callback_called);

  IS_FALSE(shimClient.error());
  END_IT
}

bool test_drop_invalid_remaining_length_message() {
  IT("drops invalid remaining length message");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x30U, 0x92U, 0x92U, 0x92U, 0x92U, 0x01U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  shimClient.respond(publish, 20U);

  rc = client.loop();

  IS_FALSE(rc);

  IS_FALSE(callback_called);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_a_message_still_arriving_is_waited_for() {
  IT("a message that has not all arrived is continued next pass rather than given up on");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(1000U);
  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  // The same PUBLISH split the way a segment boundary splits one: a header announcing 14 more
  // bytes and 8 of them, then the remaining 6.
  const uint8_t head[] = { 0x30U, 0xeU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U };
  shimClient.respond(head, 10U);
  IS_TRUE(client.loop());
  IS_FALSE(callback_called);       // Not yet - but the connection is untouched.
  IS_TRUE(client.connected());

  const uint8_t tail[] = { 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  shimClient.respond(tail, 6U);
  IS_TRUE(client.loop());

  IS_TRUE(callback_called);
  IS_TRUE(strcmp(lastTopic, "topic") == 0);
  IS_TRUE(memcmp(lastPayload, "payload", 7U) == 0);
  IS_EQUAL(lastLength, 7U);

  clearFakeMillis();
  END_IT
}

bool test_a_message_that_never_finishes_drops_the_connection() {
  IT("a message whose rest never arrives drops the connection once the socket timeout is up");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(1000U);
  PubSubClient client(server, 1883U, callback, shimClient);
  (void)client.setSocketTimeout(1U);
  IS_TRUE(client.connect("client_test1"));

  // A PUBLISH header announcing 14 more bytes, with only 8 of them delivered and no more coming.
  // Whatever the broker sent next would be read as the missing tail and then as a packet header.
  const uint8_t truncated[] = { 0x30U, 0xeU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U };
  shimClient.respond(truncated, 10U);

  IS_TRUE(client.loop());          // Still waiting for the rest.
  setFakeMillis(1000U + 1500U);    // Past the one-second socket timeout.
  const bool rc = client.loop();

  IS_FALSE(rc);
  IS_FALSE(callback_called);
  IS_FALSE(client.connected());
  IS_TRUE(client.state() == PubSubClient::State::CONNECTION_TIMEOUT);

  clearFakeMillis();
  END_IT
}

bool test_resize_buffer() {
  IT("receives a message larger than the default maximum once the buffer is grown");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  uint8_t length = 80U;  // See comment in test_receive_max_sized_message before changing this value

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.setBufferSize(static_cast<uint16_t>(length - 1U)));
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x30U, static_cast<uint8_t>(length - 2U), 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  uint8_t bigPublish[length + 1];                    // +1: bigPublish[length] holds a guard byte set below
  memset(bigPublish, 'A', length);
  bigPublish[length] = 'B';
  memcpy(bigPublish, publish, 16U);

  // One byte past the buffer it was connected with, and exactly the size of the grown one.
  IS_TRUE(client.setBufferSize(length));
  shimClient.respond(bigPublish, length);

  rc = client.loop();
  IS_TRUE(rc);

  IS_TRUE(callback_called);

  IS_TRUE(strcmp(lastTopic, "topic") == 0);
  IS_TRUE(lastLength == length - 9U);
  IS_TRUE(memcmp(lastPayload, bigPublish + 9U, lastLength) == 0);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_receive_qos1() {
  IT("receives a qos1 message");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x32U, 0x10U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x12U, 0x34U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  shimClient.respond(publish, 18U);

  const uint8_t puback[] = { 0x40U, 0x2U, 0x12U, 0x34U };
  shimClient.expect(puback, 4U);

  rc = client.loop();

  IS_TRUE(rc);

  IS_TRUE(callback_called);
  IS_TRUE(strcmp(lastTopic, "topic") == 0);
  IS_TRUE(memcmp(lastPayload, "payload", 7U) == 0);
  IS_TRUE(lastLength == 7U);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_topic_length_past_the_packet_ends_the_session() {
  IT("ends the session on a message whose topic length runs past the bytes that arrived");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  // Remaining length 4, but the topic-length field claims 0xFFFF. Both numbers come off the wire
  // and are meant to describe the same packet; one that disagrees with itself is a protocol
  // violation, and [MQTT-4.8.0-1] answers those by closing the connection.
  const uint8_t publish[] = { 0x30U, 0x04U, 0xFFU, 0xFFU, 0x41U, 0x42U };
  shimClient.respond(publish, 6U);

  rc = client.loop();

  IS_FALSE(rc);
  IS_TRUE(client.state() == PubSubClient::State::PROTOCOL_ERROR);
  IS_FALSE(client.connected());
  IS_FALSE(callback_called);
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_a_publish_too_short_for_its_topic_length_is_dropped() {
  IT("drops a PUBLISH whose remaining length cannot even hold the topic-length field");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  (void)client.setSocketTimeout(1U);                      // keep the read timeout out of the suite's runtime
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  // Remaining length 1, followed by what a PINGRESP would look like. The topic-length field is
  // two bytes, so this packet does not contain one; reading it anyway would consume the next
  // packet's header and leave the payload length wrapped just short of 4 GB.
  const uint8_t publish[] = { 0x30U, 0x01U, 0xAAU, 0xD0U, 0x00U };
  shimClient.respond(publish, 5U);

  rc = client.loop();

  IS_FALSE(rc);
  IS_FALSE(callback_called);
  IS_TRUE(client.state() == PubSubClient::State::PROTOCOL_ERROR);

  END_IT
}

bool test_a_ping_request_from_the_broker_is_ignored() {
  IT("does not answer a ping request from the broker");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(1000U);
  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));
  const uint16_t afterConnect = shimClient.received();

  // The next thing out of the client is its own ping, not an answer: a PINGRESP would fail the
  // match here, on its first byte.
  const uint8_t pingreq[] = { 0xC0U, 0x0U };
  shimClient.expect(pingreq, 2U);

  // The broker asks, which is a thing brokers do not do and clients must not answer.
  shimClient.respond(pingreq, 2U);
  setFakeMillis(2000U);
  IS_TRUE(client.loop());

  // Past a ping interval since the connect, and an answer would have put this one off past it.
  setFakeMillis(18000U);
  IS_TRUE(client.loop());
  IS_EQUAL(shimClient.received(), static_cast<uint16_t>(afterConnect + 2U));

  IS_TRUE(client.connected());
  IS_FALSE(shimClient.error());

  clearFakeMillis();
  END_IT
}

bool test_an_acknowledgement_the_link_half_took_ends_the_loop() {
  IT("reports the session lost on the pass whose acknowledgement was half written");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  const uint8_t publish[] = { 0x32U, 0x10U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x12U, 0x34U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  shimClient.respond(publish, 18U);
  // Half a PUBACK cannot be finished or taken back, so the session goes down inside this pass.
  shimClient.truncateNextWrite(2U);

  IS_FALSE(client.loop());
  IS_TRUE(client.state() == PubSubClient::State::CONNECTION_LOST);
  IS_FALSE(client.connected());

  END_IT
}

bool test_a_qos1_message_is_acknowledged_without_a_callback() {
  IT("acknowledges a qos1 message even with nothing listening for it");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  // No callback: the acknowledgement is the protocol's, and an unanswered qos1 message holds a
  // place in what the broker has in flight for the rest of the session.
  PubSubClient client(server, 1883U, nullptr, shimClient);
  IS_TRUE(client.connect("client_test1"));

  const uint8_t publish[] = { 0x32U, 0x10U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x12U, 0x34U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  shimClient.respond(publish, 18U);
  const uint8_t puback[] = { 0x40U, 0x2U, 0x12U, 0x34U };
  shimClient.expect(puback, 4U);

  const uint16_t afterConnect = shimClient.received();
  IS_TRUE(client.loop());
  IS_EQUAL(shimClient.received(), static_cast<uint16_t>(afterConnect + 4U));
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_a_publish_with_both_qos_bits_set_ends_the_session() {
  IT("ends the session on a PUBLISH at the reserved QoS level");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  // 0x36: PUBLISH with both QoS bits set, which [MQTT-3.3.1-4] forbids. Taken for a QoS 0 message
  // the packet identifier would reach the callback as the first two bytes of the payload.
  const uint8_t publish[] = { 0x36U, 0x10U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x12U, 0x34U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  shimClient.respond(publish, 18U);

  IS_FALSE(client.loop());
  IS_TRUE(client.state() == PubSubClient::State::PROTOCOL_ERROR);
  IS_FALSE(client.connected());
  IS_FALSE(callback_called);

  END_IT
}

int main() {
  SUITE("Receive");
  test_receive_callback();
  test_receive_max_sized_message();
  test_drop_invalid_remaining_length_message();
  test_a_message_still_arriving_is_waited_for();
  test_a_message_that_never_finishes_drops_the_connection();
  test_receive_oversized_message();
  test_nothing_behind_an_oversized_message_is_read();
  test_resize_buffer();
  test_receive_qos1();
  test_topic_length_past_the_packet_ends_the_session();
  test_a_publish_too_short_for_its_topic_length_is_dropped();
  test_a_ping_request_from_the_broker_is_ignored();
  test_an_acknowledgement_the_link_half_took_ends_the_loop();
  test_a_qos1_message_is_acknowledged_without_a_callback();
  test_a_publish_with_both_qos_bits_set_ends_the_session();

  FINISH
}
