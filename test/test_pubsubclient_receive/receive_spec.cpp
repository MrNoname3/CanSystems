#include "PubSubClient.h"
#include "ShimClient.h"
#include "Buffer.h"
#include "BDDTest.h"
#include "trace.h"

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
  memcpy(lastPayload, payload, length);
  lastLength = length;
}

bool test_receive_callback() {
  IT("receives a callback message");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = { 0x30U, 0xeU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
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

bool test_receive_stream() {
  IT("receives a streamed callback message");
  reset_callback();

  Stream stream;
  stream.expect((uint8_t*)"payload", 7U);

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient, stream);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = { 0x30U, 0xeU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  shimClient.respond(publish, 16U);

  rc = client.loop();

  IS_TRUE(rc);

  IS_TRUE(callback_called);
  IS_TRUE(strcmp(lastTopic, "topic") == 0);
  IS_TRUE(lastLength == 7U);

  IS_FALSE(stream.error());
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_receive_max_sized_message() {
  IT("receives an max-sized message");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  uint8_t length = 80U;  // If this is changed to > 128 then the publish packet below
                         // is no longer valid as it assumes the remaining length
                         // is a single-uint8_t. Don't make that mistake like I just
                         // did and lose a whole evening tracking down the issue.
  IS_TRUE(client.setBufferSize(length));
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = { 0x30U, static_cast<uint8_t>(length - 2U), 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  uint8_t bigPublish[length];
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
  IT("drops an oversized message");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  uint8_t length = 80U;  // See comment in test_receive_max_sized_message before changing this value

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.setBufferSize(static_cast<uint16_t>(length - 1U)));
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = { 0x30U, static_cast<uint8_t>(length - 2U), 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  uint8_t bigPublish[length];
  memset(bigPublish, 'A', length);
  bigPublish[length] = 'B';
  memcpy(bigPublish, publish, 16U);
  shimClient.respond(bigPublish, length);

  rc = client.loop();

  IS_TRUE(rc);

  IS_FALSE(callback_called);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_drop_invalid_remaining_length_message() {
  IT("drops invalid remaining length message");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = { 0x30U, 0x92U, 0x92U, 0x92U, 0x92U, 0x01U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  shimClient.respond(publish, 20U);

  rc = client.loop();

  IS_FALSE(rc);

  IS_FALSE(callback_called);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_resize_buffer() {
  IT("receives a message larger than the default maximum");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  uint8_t length = 80U;  // See comment in test_receive_max_sized_message before changing this value

  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.setBufferSize(static_cast<uint16_t>(length - 1U)));
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = { 0x30U, static_cast<uint8_t>(length - 2U), 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  uint8_t bigPublish[length];
  memset(bigPublish, 'A', length);
  bigPublish[length] = 'B';
  memcpy(bigPublish, publish, 16U);
  // Send it twice
  shimClient.respond(bigPublish, length);
  shimClient.respond(bigPublish, length);

  rc = client.loop();
  IS_TRUE(rc);

  // First message fails as it is too big
  IS_FALSE(callback_called);

  // Resize the buffer
  IS_TRUE(client.setBufferSize(length));

  rc = client.loop();
  IS_TRUE(rc);

  IS_TRUE(callback_called);

  IS_TRUE(strcmp(lastTopic, "topic") == 0);
  IS_TRUE(lastLength == length - 9U);
  IS_TRUE(memcmp(lastPayload, bigPublish + 9U, lastLength) == 0);

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_receive_oversized_stream_message() {
  IT("receive an oversized streamed message");
  reset_callback();

  Stream stream;

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  uint8_t length = 80U;  // See comment in test_receive_max_sized_message before changing this value

  PubSubClient client(server, 1883U, callback, shimClient, stream);
  IS_TRUE(client.setBufferSize(static_cast<uint16_t>(length - 1U)));
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = { 0x30U, static_cast<uint8_t>(length - 2U), 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };

  uint8_t bigPublish[length];
  memset(bigPublish, 'A', length);
  bigPublish[length] = 'B';
  memcpy(bigPublish, publish, 16U);

  shimClient.respond(bigPublish, length);
  stream.expect(bigPublish + 9U, static_cast<uint16_t>(length - 9U));

  rc = client.loop();

  IS_TRUE(rc);

  IS_TRUE(callback_called);
  IS_TRUE(strcmp(lastTopic, "topic") == 0);

  IS_TRUE(lastLength == length - 10U);

  IS_FALSE(stream.error());
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_receive_qos1() {
  IT("receives a qos1 message");
  reset_callback();

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = { 0x32U, 0x10U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x12U, 0x34U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  shimClient.respond(publish, 18U);

  uint8_t puback[] = { 0x40U, 0x2U, 0x12U, 0x34U };
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

int main() {
  SUITE("Receive");
  test_receive_callback();
  test_receive_stream();
  test_receive_max_sized_message();
  test_drop_invalid_remaining_length_message();
  test_receive_oversized_message();
  test_resize_buffer();
  test_receive_oversized_stream_message();
  test_receive_qos1();

  FINISH
}
