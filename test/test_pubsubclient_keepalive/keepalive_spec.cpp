#include "PubSubClient.h"
#include "ShimClient.h"
#include "Buffer.h"
#include "BDDTest.h"
#include "trace.h"
#include "Arduino.h"

uint8_t server[] = { 172U, 16U, 0U, 2U };

// Keep-alive timing is driven entirely by millis(); the shim's fake clock lets these tests
// advance virtual time instantly instead of sleeping in real time. tickMs mirrors the one-second
// real-time step the suite used before: each iteration moves the fake clock forward by exactly
// keepAlive/15 seconds, so the ping boundaries land on the same iterations (i == 15, 31, 47) the
// PubSubClient keep-alive logic (default 15 s) would hit with wall-clock sleeps. setFakeMillis()
// must be called before connect() so lastInActivity starts on the fake clock, not real time.
namespace {
  constexpr uint32_t baseMs = 1000U;  // Non-zero fake-clock start; the value itself is irrelevant.
  constexpr uint32_t tickMs = 1000U;  // Virtual time advanced per loop() iteration (was sleep(1)).
}  // namespace

void callback([[maybe_unused]] char* topic, [[maybe_unused]] uint8_t* payload, [[maybe_unused]] unsigned int length) {
  // handle message arrived
}

bool test_keepalive_pings_idle() {
  IT("keeps an idle connection alive");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  uint32_t now = baseMs;

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t pingreq[] = { 0xC0U, 0x0U };
  shimClient.expect(pingreq, 2U);
  const uint8_t pingresp[] = { 0xD0U, 0x0U };
  shimClient.respond(pingresp, 2U);

  for(uint8_t i = 0U; i < 50U; i++) {
    now += tickMs;
    setFakeMillis(now);
    if(i == 15U || i == 31U || i == 47U) {
      shimClient.expect(pingreq, 2U);
      shimClient.respond(pingresp, 2U);
    }
    rc = client.loop();
    IS_TRUE(rc);
  }

  IS_FALSE(shimClient.error());

  clearFakeMillis();
  END_IT
}

bool test_keepalive_pings_with_outbound_qos0() {
  IT("keeps a connection alive that only sends qos0");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  uint32_t now = baseMs;

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x30U, 0xeU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };

  for(uint8_t i = 0U; i < 50U; i++) {
    TRACE(i << ":");
    shimClient.expect(publish, 16U);
    rc = client.publish("topic", "payload");
    IS_TRUE(rc);
    IS_FALSE(shimClient.error());
    now += tickMs;
    setFakeMillis(now);
    if(i == 15U || i == 31U || i == 47U) {
      const uint8_t pingreq[] = { 0xC0U, 0x0U };
      shimClient.expect(pingreq, 2U);
      const uint8_t pingresp[] = { 0xD0U, 0x0U };
      shimClient.respond(pingresp, 2U);
    }
    rc = client.loop();
    IS_TRUE(rc);
    IS_FALSE(shimClient.error());
  }

  clearFakeMillis();
  END_IT
}

bool test_keepalive_pings_with_inbound_qos0() {
  IT("keeps a connection alive that only receives qos0");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  uint32_t now = baseMs;

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x30U, 0xeU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };

  for(uint8_t i = 0U; i < 50U; i++) {
    TRACE(i << ":");
    now += tickMs;
    setFakeMillis(now);
    if(i == 15U || i == 31U || i == 47U) {
      const uint8_t pingreq[] = { 0xC0U, 0x0U };
      shimClient.expect(pingreq, 2U);
      const uint8_t pingresp[] = { 0xD0U, 0x0U };
      shimClient.respond(pingresp, 2U);
    }
    shimClient.respond(publish, 16U);
    rc = client.loop();
    IS_TRUE(rc);
    IS_FALSE(shimClient.error());
  }

  clearFakeMillis();
  END_IT
}

bool test_keepalive_no_pings_inbound_qos1() {
  IT("does not send pings for connections with inbound qos1");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  uint32_t now = baseMs;

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t publish[] = { 0x32U, 0x10U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x12U, 0x34U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  const uint8_t puback[] = { 0x40U, 0x2U, 0x12U, 0x34U };

  for(uint8_t i = 0U; i < 50U; i++) {
    shimClient.respond(publish, 18U);
    shimClient.expect(puback, 4U);
    now += tickMs;
    setFakeMillis(now);
    rc = client.loop();
    IS_TRUE(rc);
    IS_FALSE(shimClient.error());
  }

  clearFakeMillis();
  END_IT
}

bool test_keepalive_disconnects_hung() {
  IT("disconnects a hung connection");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  uint32_t now = baseMs;

  PubSubClient client(server, 1883U, callback, shimClient);
  bool rc = client.connect("client_test1");
  IS_TRUE(rc);

  const uint8_t pingreq[] = { 0xC0U, 0x0U };
  shimClient.expect(pingreq, 2U);

  for(uint8_t i = 0U; i < 32U; i++) {
    now += tickMs;
    setFakeMillis(now);
    rc = client.loop();
  }
  IS_FALSE(rc);

  PubSubClient::State state = client.state();
  IS_TRUE(state == PubSubClient::State::CONNECTION_TIMEOUT);

  IS_FALSE(shimClient.error());

  clearFakeMillis();
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
