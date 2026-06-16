#include "PubSubClient.h"
#include "ShimClient.h"
#include "Buffer.h"
#include "BDDTest.h"
#include "trace.h"

uint8_t server[] = { 172U, 16U, 0U, 2U };

void callback([[maybe_unused]] char* topic, [[maybe_unused]] uint8_t* payload, [[maybe_unused]] unsigned int length) {
  // handle message arrived
}

bool test_connect_fails_no_network() {
  IT("fails to connect if underlying client doesn't connect");
  ShimClient shimClient;
  shimClient.setAllowConnect(false);
  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_FALSE(rc);
  PubSubClient::State state = client.state();
  IS_TRUE(state == PubSubClient::State::CONNECT_FAILED);
  END_IT
}

bool test_connect_fails_on_no_response() {
  IT("fails to connect if no response received after 15 seconds");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);
  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_FALSE(rc);
  PubSubClient::State state = client.state();
  IS_TRUE(state == PubSubClient::State::CONNECTION_TIMEOUT);
  END_IT
}

bool test_connect_properly_formatted() {
  IT("sends a properly formatted connect packet and succeeds");
  ShimClient shimClient;

  shimClient.setAllowConnect(true);
  uint8_t expectServer[] = { 172U, 16U, 0U, 2U };
  shimClient.expectConnect(expectServer, 1883);
  const uint8_t connect[] = { 0x10U, 0x18U, 0x0U, 0x4U, 0x4dU, 0x51U, 0x54U, 0x54U, 0x4U, 0x2U, 0x0U, 0xfU, 0x0U, 0xcU, 0x63U, 0x6cU, 0x69U, 0x65U, 0x6eU, 0x74U, 0x5fU, 0x74U, 0x65U, 0x73U, 0x74U, 0x31U };
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };

  shimClient.expect(connect, 26U);
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  PubSubClient::State state = client.state();
  IS_TRUE(state == PubSubClient::State::DISCONNECTED);

  bool rc = client.connect("client_test1");
  IS_TRUE(rc);
  IS_FALSE(shimClient.error());

  state = client.state();
  IS_TRUE(state == PubSubClient::State::CONNECTED);

  END_IT
}

bool test_connect_properly_formatted_hostname() {
  IT("accepts a hostname");
  ShimClient shimClient;

  shimClient.setAllowConnect(true);
  shimClient.expectConnect("localhost", 1883U);
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client("localhost", 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_connect_fails_on_bad_rc() {
  IT("fails to connect if a bad return code is received");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x01U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_FALSE(rc);

  PubSubClient::State state = client.state();
  IS_TRUE(state == PubSubClient::State::CONNECT_BAD_PROTOCOL);

  END_IT
}

bool test_connect_non_clean_session() {
  IT("sends a properly formatted non-clean session connect packet and succeeds");
  ShimClient shimClient;

  shimClient.setAllowConnect(true);
  uint8_t expectServer[] = { 172U, 16U, 0U, 2U };
  shimClient.expectConnect(expectServer, 1883);
  const uint8_t connect[] = { 0x10U, 0x18U, 0x0U, 0x4U, 0x4dU, 0x51U, 0x54U, 0x54U, 0x4U, 0x0U, 0x0U, 0xfU, 0x0U, 0xcU, 0x63U, 0x6cU, 0x69U, 0x65U, 0x6eU, 0x74U, 0x5fU, 0x74U, 0x65U, 0x73U, 0x74U, 0x31U };
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };

  shimClient.expect(connect, 26U);
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  PubSubClient::State state = client.state();
  IS_TRUE(state == PubSubClient::State::DISCONNECTED);

  bool rc = client.connect("client_test1", nullptr, nullptr, nullptr, 0U, false, nullptr, false);
  IS_TRUE(rc);
  IS_FALSE(shimClient.error());

  state = client.state();
  IS_TRUE(state == PubSubClient::State::CONNECTED);

  END_IT
}

bool test_connect_accepts_username_password() {
  IT("accepts a username and password");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connect[] = { 0x10U, 0x24U, 0x0U, 0x4U, 0x4dU, 0x51U, 0x54U, 0x54U, 0x4U, 0xc2U, 0x0U, 0xfU, 0x0U, 0xcU, 0x63U, 0x6cU, 0x69U, 0x65U, 0x6eU, 0x74U, 0x5fU, 0x74U, 0x65U, 0x73U, 0x74U, 0x31U, 0x0U, 0x4U, 0x75U, 0x73U, 0x65U, 0x72U, 0x0U, 0x4U, 0x70U, 0x61U, 0x73U, 0x73U };
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.expect(connect, 0x26U);
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1", "user", "pass");
  IS_TRUE(rc);
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_connect_accepts_username_no_password() {
  IT("accepts a username but no password");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connect[] = { 0x10U, 0x1eU, 0x0U, 0x4U, 0x4dU, 0x51U, 0x54U, 0x54U, 0x4U, 0x82U, 0x0U, 0xfU, 0x0U, 0xcU, 0x63U, 0x6cU, 0x69U, 0x65U, 0x6eU, 0x74U, 0x5fU, 0x74U, 0x65U, 0x73U, 0x74U, 0x31U, 0x0U, 0x4U, 0x75U, 0x73U, 0x65U, 0x72U };
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.expect(connect, 0x20U);
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1", "user", nullptr);
  IS_TRUE(rc);
  IS_FALSE(shimClient.error());

  END_IT
}
bool test_connect_accepts_username_blank_password() {
  IT("accepts a username and blank password");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connect[] = { 0x10U, 0x20U, 0x0U, 0x4U, 0x4dU, 0x51U, 0x54U, 0x54U, 0x4U, 0xc2U, 0x0U, 0xfU, 0x0U, 0xcU, 0x63U, 0x6cU, 0x69U, 0x65U, 0x6eU, 0x74U, 0x5fU, 0x74U, 0x65U, 0x73U, 0x74U, 0x31U, 0x0U, 0x4U, 0x75U, 0x73U, 0x65U, 0x72U, 0x0U, 0x0U };
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.expect(connect, 0x26U);
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1", "user", "pass");
  IS_TRUE(rc);
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_connect_ignores_password_no_username() {
  IT("ignores a password but no username");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connect[] = { 0x10U, 0x18U, 0x0U, 0x4U, 0x4dU, 0x51U, 0x54U, 0x54U, 0x4U, 0x2U, 0x0U, 0xfU, 0x0U, 0xcU, 0x63U, 0x6cU, 0x69U, 0x65U, 0x6eU, 0x74U, 0x5fU, 0x74U, 0x65U, 0x73U, 0x74U, 0x31U };
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.expect(connect, 26U);
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1", nullptr, "pass");
  IS_TRUE(rc);
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_connect_with_will() {
  IT("accepts a will");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connect[] = { 0x10U, 0x30U, 0x0U, 0x4U, 0x4dU, 0x51U, 0x54U, 0x54U, 0x4U, 0xeU, 0x0U, 0xfU, 0x0U, 0xcU, 0x63U, 0x6cU, 0x69U, 0x65U, 0x6eU, 0x74U, 0x5fU, 0x74U, 0x65U, 0x73U, 0x74U, 0x31U, 0x0U, 0x9U, 0x77U, 0x69U, 0x6cU, 0x6cU, 0x54U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x0U, 0xbU, 0x77U, 0x69U, 0x6cU, 0x6cU, 0x4dU, 0x65U, 0x73U, 0x73U, 0x61U, 0x67U, 0x65U };
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.expect(connect, 0x32U);
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1", "willTopic", 1U, false, "willMessage");
  IS_TRUE(rc);
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_connect_with_will_username_password() {
  IT("accepts a will, username and password");
  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connect[] = { 0x10U, 0x40U, 0x0U, 0x4U, 0x4dU, 0x51U, 0x54U, 0x54U, 0x4U, 0xceU, 0x0U, 0xfU, 0x0U, 0xcU, 0x63U, 0x6cU, 0x69U, 0x65U, 0x6eU, 0x74U, 0x5fU, 0x74U, 0x65U, 0x73U, 0x74U, 0x31U, 0x0U, 0x9U, 0x77U, 0x69U, 0x6cU, 0x6cU, 0x54U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x0U, 0xbU, 0x77U, 0x69U, 0x6cU, 0x6cU, 0x4dU, 0x65U, 0x73U, 0x73U, 0x61U, 0x67U, 0x65U, 0x0U, 0x4U, 0x75U, 0x73U, 0x65U, 0x72U, 0x0U, 0x8U, 0x70U, 0x61U, 0x73U, 0x73U, 0x77U, 0x6fU, 0x72U, 0x64U };
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.expect(connect, 0x42U);
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1", "user", "password", "willTopic", 1U, false, "willMessage");
  IS_TRUE(rc);
  IS_FALSE(shimClient.error());

  END_IT
}

bool test_connect_disconnect_connect() {
  IT("connects, disconnects and connects again");
  ShimClient shimClient;

  shimClient.setAllowConnect(true);
  uint8_t expectServer[] = { 172U, 16U, 0U, 2U };
  shimClient.expectConnect(expectServer, 1883);
  const uint8_t connect[] = { 0x10U, 0x18U, 0x0U, 0x4U, 0x4dU, 0x51U, 0x54U, 0x54U, 0x4U, 0x2U, 0x0U, 0xfU, 0x0U, 0xcU, 0x63U, 0x6cU, 0x69U, 0x65U, 0x6eU, 0x74U, 0x5fU, 0x74U, 0x65U, 0x73U, 0x74U, 0x31U };
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };

  shimClient.expect(connect, 26U);
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);

  PubSubClient::State state = client.state();
  IS_TRUE(state == PubSubClient::State::DISCONNECTED);

  bool rc = client.connect("client_test1");
  IS_TRUE(rc);
  IS_FALSE(shimClient.error());

  state = client.state();
  IS_TRUE(state == PubSubClient::State::CONNECTED);

  const uint8_t disconnect[] = { 0xE0U, 0x00U };
  shimClient.expect(disconnect, 2U);

  client.disconnect();

  IS_FALSE(client.connected());
  IS_FALSE(shimClient.connected());
  IS_FALSE(shimClient.error());

  state = client.state();
  IS_TRUE(state == PubSubClient::State::DISCONNECTED);

  shimClient.expect(connect, 26U);
  shimClient.respond(connack, 4U);
  rc = client.connect("client_test1");
  IS_TRUE(rc);
  IS_FALSE(shimClient.error());
  state = client.state();
  IS_TRUE(state == PubSubClient::State::CONNECTED);

  END_IT
}

bool test_connect_custom_keepalive() {
  IT("sends a properly formatted connect packet with custom keepalive value");
  ShimClient shimClient;

  shimClient.setAllowConnect(true);
  uint8_t expectServer[] = { 172U, 16U, 0U, 2U };
  shimClient.expectConnect(expectServer, 1883U);

  // Set keepalive to 300secs == 0x01 0x2c
  const uint8_t connect[] = { 0x10U, 0x18U, 0x0U, 0x4U, 0x4dU, 0x51U, 0x54U, 0x54U, 0x4U, 0x2U, 0x01U, 0x2cU, 0x0U, 0xcU, 0x63U, 0x6cU, 0x69U, 0x65U, 0x6eU, 0x74U, 0x5fU, 0x74U, 0x65U, 0x73U, 0x74U, 0x31U };
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };

  shimClient.expect(connect, 26U);
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  PubSubClient::State state = client.state();
  IS_TRUE(state == PubSubClient::State::DISCONNECTED);

  client.setKeepAlive(300U);

  bool rc = client.connect("client_test1");
  IS_TRUE(rc);
  IS_FALSE(shimClient.error());

  state = client.state();
  IS_TRUE(state == PubSubClient::State::CONNECTED);

  END_IT
}

int main() {
  SUITE("Connect");

  test_connect_fails_no_network();
  test_connect_fails_on_no_response();

  test_connect_properly_formatted();
  test_connect_non_clean_session();
  test_connect_accepts_username_password();
  test_connect_fails_on_bad_rc();
  test_connect_properly_formatted_hostname();

  test_connect_accepts_username_no_password();
  test_connect_ignores_password_no_username();
  test_connect_with_will();
  test_connect_with_will_username_password();
  test_connect_disconnect_connect();

  test_connect_custom_keepalive();
  FINISH
}
