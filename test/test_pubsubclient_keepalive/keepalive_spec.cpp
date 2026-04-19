#include "PubSubClient.h"
#include "ShimClient.h"
#include "Buffer.h"
#include "BDDTest.h"
#include "trace.h"
#include <unistd.h>

uint8_t server[] = { 172U, 16U, 0U, 2U };

void callback([[maybe_unused]] char* topic, [[maybe_unused]] uint8_t* payload, [[maybe_unused]] unsigned int length) {
  // handle message arrived
}

bool test_keepalive_pings_idle() {
  IT("keeps an idle connection alive (takes 1 minute)");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t pingreq[] = { 0xC0U, 0x0U };
  shimClient.expect(pingreq, 2U);
  uint8_t pingresp[] = { 0xD0U, 0x0U };
  shimClient.respond(pingresp, 2U);

  for (uint8_t i = 0U; i < 50U; i++) {
    sleep(1);
    if (i == 15U || i == 31U || i == 47U) {
      shimClient.expect(pingreq, 2U);
      shimClient.respond(pingresp, 2U);
    }
    rc = client.loop();
    IS_TRUE(rc);
  }

  IS_FALSE(shimClient.error());

  END_IT
}

bool test_keepalive_pings_with_outbound_qos0() {
  IT("keeps a connection alive that only sends qos0 (takes 1 minute)");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = { 0x30U, 0xeU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };

  for (uint8_t i = 0U; i < 50U; i++) {
    TRACE(i << ":");
    shimClient.expect(publish, 16U);
    rc = client.publish("topic", "payload");
    IS_TRUE(rc);
    IS_FALSE(shimClient.error());
    sleep(1);
    if (i == 15U || i == 31U || i == 47U) {
      uint8_t pingreq[] = { 0xC0U, 0x0U };
      shimClient.expect(pingreq, 2U);
      uint8_t pingresp[] = { 0xD0U, 0x0U };
      shimClient.respond(pingresp, 2U);
    }
    rc = client.loop();
    IS_TRUE(rc);
    IS_FALSE(shimClient.error());
  }

  END_IT
}

bool test_keepalive_pings_with_inbound_qos0() {
  IT("keeps a connection alive that only receives qos0 (takes 1 minute)");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = { 0x30U, 0xeU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };

  for (uint8_t i = 0U; i < 50U; i++) {
    TRACE(i << ":");
    sleep(1);
    if (i == 15U || i == 31U || i == 47U) {
      uint8_t pingreq[] = { 0xC0U, 0x0U };
      shimClient.expect(pingreq, 2U);
      uint8_t pingresp[] = { 0xD0U, 0x0U };
      shimClient.respond(pingresp, 2U);
    }
    shimClient.respond(publish, 16U);
    rc = client.loop();
    IS_TRUE(rc);
    IS_FALSE(shimClient.error());
  }

  END_IT
}

bool test_keepalive_no_pings_inbound_qos1() {
  IT("does not send pings for connections with inbound qos1 (takes 1 minute)");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t publish[] = { 0x32U, 0x10U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x12U, 0x34U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  uint8_t puback[] = { 0x40U, 0x2U, 0x12U, 0x34U };

  for (uint8_t i = 0U; i < 50U; i++) {
    shimClient.respond(publish, 18U);
    shimClient.expect(puback, 4U);
    sleep(1);
    rc = client.loop();
    IS_TRUE(rc);
    IS_FALSE(shimClient.error());
  }

  END_IT
}

bool test_keepalive_disconnects_hung() {
  IT("disconnects a hung connection (takes 30 seconds)");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  uint8_t pingreq[] = { 0xC0U, 0x0U };
  shimClient.expect(pingreq, 2U);

  for (uint8_t i = 0U; i < 32U; i++) {
    sleep(1U);
    rc = client.loop();
  }
  IS_FALSE(rc);

  PubSubClient::State state = client.state();
  IS_TRUE(state == PubSubClient::State::CONNECTION_TIMEOUT);

  IS_FALSE(shimClient.error());

  END_IT
}

int main() {
  SUITE("Keep-alive");
  test_keepalive_pings_idle();
  test_keepalive_pings_with_outbound_qos0();
  test_keepalive_pings_with_inbound_qos0();
  test_keepalive_no_pings_inbound_qos1();
  test_keepalive_disconnects_hung();

  FINISH
}
