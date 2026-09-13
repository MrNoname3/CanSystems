#include "PubSubClient.h"
#include "ShimClient.h"
#include "Buffer.h"
#include "BDDTest.h"
#include "trace.h"
#include <string.h>

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

  const uint8_t subscribe[] = { 0x82U, 0xaU, 0x0U, 0x1U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x0U };
  shimClient.expect(subscribe, 12U);
  const uint8_t suback[] = { 0x90U, 0x3U, 0x0U, 0x1U, 0x0U };
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

  const uint8_t subscribe[] = { 0x82U, 0xaU, 0x0U, 0x1U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x1U };
  shimClient.expect(subscribe, 12U);
  const uint8_t suback[] = { 0x90U, 0x3U, 0x0U, 0x1U, 0x1U };
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

  const uint8_t suback[] = { 0x90U, 0x3U, 0x0U, 0x1U, 0x0U };
  shimClient.respond(suback, 5U);

  // max length should be allowed
  //                            0        1         2         3         4         5         6         7         8         9         0         1         2
  rc = client.subscribe("1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678");
  IS_TRUE(rc);

  //                            0        1         2         3         4         5         6         7         8         9         0         1         2
  // A filter the buffer cannot hold is refused before anything is written, which is what tells it
  // apart from one that went out and was never acknowledged: with nothing expected from here on,
  // a write of any kind is an error the shim records.
  shimClient.expect(nullptr, 0U);
  rc = client.subscribe("12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789");
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

  const uint8_t unsubscribe[] = { 0xA2U, 0x9U, 0x0U, 0x1U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U };
  shimClient.expect(unsubscribe, 11U);
  const uint8_t unsuback[] = { 0xB0U, 0x2U, 0x0U, 0x1U };
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

bool test_subscribe_refused_by_the_broker() {
  IT("reports a subscription the broker would not grant");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  // 0x80 is what a broker answers for a filter its access rules do not allow.
  const uint8_t suback[] = { 0x90U, 0x3U, 0x0U, 0x1U, 0x80U };
  shimClient.respond(suback, 5U);

  rc = client.subscribe("topic", 1U);
  IS_FALSE(rc);
  // Only the filter was refused: the session itself is still up for the caller to decide about.
  IS_TRUE(client.connected());

  END_IT
}

bool test_subscribe_unanswered() {
  IT("reports a subscription the broker never answers");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  // The wait runs on the real clock, as the CONNACK's does; a second of it is enough to show.
  client.setSocketTimeout(1U);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  rc = client.subscribe("topic", 1U);
  IS_FALSE(rc);
  IS_TRUE(client.state() == PubSubClient::State::CONNECTION_TIMEOUT);

  END_IT
}

bool test_subscribe_half_written_ends_the_session() {
  IT("a subscribe the link took only part of ends the session");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  // No SUBACK is queued: the wait must never be reached, the half-written packet having ended it.
  shimClient.truncateNextWrite(4U);
  rc = client.subscribe("topic", 1U);
  IS_FALSE(rc);
  IS_TRUE(client.state() == PubSubClient::State::CONNECTION_LOST);

  END_IT
}

bool test_subscribe_filling_the_whole_buffer() {
  IT("a filter that leaves no room for the qos byte is refused");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  // Nine bytes short of the buffer is the length the packet fits in up to its last byte, and the
  // qos byte then goes one past the end of it.
  static char topic[1024];
  memset(topic, 'a', 1015U);
  topic[1015] = '\0';

  shimClient.expect(nullptr, 0U);
  rc = client.subscribe(topic, 1U);
  IS_FALSE(rc);
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_packet_ids_step_past_zero_to_one() {
  IT("steps the packet id past zero to one, zero not being one a packet may carry");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  // Unsubscribe takes no answer, so it is the cheap way to spend the ids up to the last one.
  bool allSent = true;
  for(uint32_t i = 0U; i < 65534U; i++) {
    allSent = client.unsubscribe("topic") && allSent;
  }
  IS_TRUE(allSent);

  const uint8_t lastId[] = { 0xA2U, 0x9U, 0xFFU, 0xFFU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U };
  shimClient.expect(lastId, 11U);
  IS_TRUE(client.unsubscribe("topic"));

  const uint8_t wrapped[] = { 0xA2U, 0x9U, 0x0U, 0x1U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U };
  shimClient.expect(wrapped, 11U);
  IS_TRUE(client.unsubscribe("topic"));

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_subscribe_refuses_a_filter_the_standard_forbids() {
  IT("refuses a filter that is empty or misplaces a wildcard");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  const uint16_t afterConnect = shimClient.received();

  // A filter is at least one character [MQTT-4.7.3-1]; '#' stands alone or follows a separator and
  // is the last character [MQTT-4.7.1-2]; '+' occupies an entire level [MQTT-4.7.1-3].
  IS_FALSE(client.subscribe("", 0U));
  IS_FALSE(client.subscribe("sport/tennis#", 0U));
  IS_FALSE(client.subscribe("sport/#/player", 0U));
  IS_FALSE(client.subscribe("sport+", 0U));
  IS_FALSE(client.unsubscribe(""));
  IS_FALSE(client.unsubscribe("sport/tennis#"));

  IS_EQUAL(shimClient.received(), afterConnect);
  IS_TRUE(client.connected());
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_subscribe_accepts_the_wildcards_the_standard_allows() {
  IT("accepts a filter whose wildcards sit where they belong");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  const uint8_t suback[] = { 0x90U, 0x03U, 0x00U, 0x01U, 0x00U };
  shimClient.respond(suback, 5U);
  IS_TRUE(client.subscribe("sport/tennis/+", 0U));

  const uint8_t suback2[] = { 0x90U, 0x03U, 0x00U, 0x02U, 0x00U };
  shimClient.respond(suback2, 5U);
  IS_TRUE(client.subscribe("#", 0U));

  const uint8_t suback3[] = { 0x90U, 0x03U, 0x00U, 0x03U, 0x00U };
  shimClient.respond(suback3, 5U);
  IS_TRUE(client.subscribe("sport/+/player1/#", 0U));

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_subscribe_carries_on_a_half_read_message() {
  IT("finishes a message the reader was part way through and still finds its SUBACK");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  // The first 8 bytes of a 14-byte PUBLISH: the reader keeps its place and waits for the rest.
  const uint8_t head[] = { 0x30U, 0x0CU, 0x00U, 0x05U, 0x74U, 0x6fU, 0x70U, 0x69U };
  shimClient.respond(head, 8U);
  IS_TRUE(client.loop());

  // The rest of it, and behind that the answer to the subscription below. Starting the reader over
  // would take the six bytes left of the message for a packet header and lose the stream.
  const uint8_t tail[] = { 0x63U, 0x68U, 0x65U, 0x6cU, 0x6cU, 0x6fU };
  shimClient.respond(tail, 6U);
  const uint8_t suback[] = { 0x90U, 0x03U, 0x00U, 0x01U, 0x00U };
  shimClient.respond(suback, 5U);

  IS_TRUE(client.subscribe("topic", 0U));
  IS_TRUE(client.connected());
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
  test_subscribe_filling_the_whole_buffer();
  test_subscribe_refused_by_the_broker();
  test_subscribe_unanswered();
  test_subscribe_half_written_ends_the_session();
  test_unsubscribe();
  test_unsubscribe_not_connected();
  test_packet_ids_step_past_zero_to_one();
  test_subscribe_refuses_a_filter_the_standard_forbids();
  test_subscribe_accepts_the_wildcards_the_standard_allows();
  test_subscribe_carries_on_a_half_read_message();

  FINISH
}
