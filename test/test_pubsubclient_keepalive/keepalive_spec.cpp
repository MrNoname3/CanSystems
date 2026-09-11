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

bool test_keepalive_retries_a_refused_ping() {
  IT("sends the ping again at once when the client refused to take it");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  // Past the keep-alive boundary, so the next loop() owes the broker a ping.
  setFakeMillis(baseMs + (16U * tickMs));
  shimClient.failNextWrites(1U);
  const uint16_t beforeRefusal = shimClient.received();
  IS_TRUE(client.loop());
  IS_EQUAL(shimClient.received(), beforeRefusal);   // Nothing reached the client.

  // A ping the client would not take is still owed, so a later pass has to try again rather than
  // wait out another keep-alive interval in silence.
  setFakeMillis(baseMs + (17U * tickMs));
  const uint8_t pingreq[] = { 0xC0U, 0x0U };
  shimClient.expect(pingreq, 2U);
  const uint8_t pingresp[] = { 0xD0U, 0x0U };
  shimClient.respond(pingresp, 2U);
  IS_TRUE(client.loop());
  IS_EQUAL(shimClient.received(), static_cast<uint16_t>(beforeRefusal + 2U));

  IS_FALSE(shimClient.error());

  clearFakeMillis();
  END_IT
}

bool test_keepalive_waits_before_asking_the_client_again() {
  IT("does not ask again within the retry interval after a refusal");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  setFakeMillis(baseMs + (16U * tickMs));
  shimClient.failNextWrites(1U);
  IS_TRUE(client.loop());
  const uint16_t afterRefusal = shimClient.received();

  // Half a second on, the client would take it now - but asking again this soon would mean asking
  // on every pass of the caller's loop, hundreds of times a second, for the rest of the interval.
  setFakeMillis(baseMs + (16U * tickMs) + 500U);
  IS_TRUE(client.loop());
  IS_EQUAL(shimClient.received(), afterRefusal);

  // A second after the refusal it is worth asking again.
  setFakeMillis(baseMs + (17U * tickMs));
  const uint8_t pingreq[] = { 0xC0U, 0x0U };
  shimClient.expect(pingreq, 2U);
  const uint8_t pingresp[] = { 0xD0U, 0x0U };
  shimClient.respond(pingresp, 2U);
  IS_TRUE(client.loop());
  IS_EQUAL(shimClient.received(), static_cast<uint16_t>(afterRefusal + 2U));

  IS_FALSE(shimClient.error());

  clearFakeMillis();
  END_IT
}

bool test_keepalive_counts_the_pings_the_client_refused() {
  IT("counts one refusal per ping the client would not take, not one per attempt");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));
  IS_EQUAL(client.getRefusedPingCount(), 0U);

  const uint8_t pingreq[] = { 0xC0U, 0x0U };
  const uint8_t pingresp[] = { 0xD0U, 0x0U };

  // One ping refused twice before it goes out is one ping the client would not take.
  setFakeMillis(baseMs + (16U * tickMs));
  shimClient.failNextWrites(2U);
  IS_TRUE(client.loop());
  setFakeMillis(baseMs + (17U * tickMs));
  IS_TRUE(client.loop());
  setFakeMillis(baseMs + (18U * tickMs));
  shimClient.expect(pingreq, 2U);
  shimClient.respond(pingresp, 2U);
  IS_TRUE(client.loop());
  IS_EQUAL(client.getRefusedPingCount(), 1U);

  // The next interval's ping is refused too: a second ping, so a second count.
  setFakeMillis(baseMs + (34U * tickMs));
  shimClient.failNextWrites(1U);
  IS_TRUE(client.loop());
  setFakeMillis(baseMs + (35U * tickMs));
  shimClient.expect(pingreq, 2U);
  shimClient.respond(pingresp, 2U);
  IS_TRUE(client.loop());
  IS_EQUAL(client.getRefusedPingCount(), 2U);

  IS_FALSE(shimClient.error());

  clearFakeMillis();
  END_IT
}

bool test_keepalive_gives_up_on_a_client_that_never_takes_the_ping() {
  IT("reports a timeout when the client refuses the ping for a whole keep-alive interval");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  // Retrying is only right while it can still come to something; a client that never takes the
  // ping is as dead as one that never answers it, and has to end the same way.
  shimClient.failNextWrites(100U);
  setFakeMillis(baseMs + (16U * tickMs));
  IS_TRUE(client.loop());
  setFakeMillis(baseMs + (32U * tickMs));
  IS_FALSE(client.loop());
  IS_TRUE(client.state() == PubSubClient::State::CONNECTION_TIMEOUT);

  clearFakeMillis();
  END_IT
}

bool test_keepalive_starts_the_refusal_deadline_over_on_reconnect() {
  IT("a reconnected session gets its own grace for a ping the client will not take");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  // The session ends with a ping the client would not take still waiting to go out.
  setFakeMillis(baseMs + (16U * tickMs));
  shimClient.failNextWrites(1U);
  IS_TRUE(client.loop());
  IS_EQUAL(client.getRefusedPingCount(), 1U);

  shimClient.setConnected(false);
  IS_FALSE(client.loop());

  // A minute later the link is back and a fresh CONNACK opens a session of its own.
  setFakeMillis(baseMs + (76U * tickMs));
  shimClient.setConnected(true);
  shimClient.setAllowConnect(true);
  shimClient.respond(connack, 4U);
  IS_TRUE(client.connect("client_test1"));

  // Its first refused ping is the first of this session: worth retrying, and worth counting.
  setFakeMillis(baseMs + (92U * tickMs));
  shimClient.failNextWrites(1U);
  IS_TRUE(client.loop());
  IS_EQUAL(client.getRefusedPingCount(), 2U);

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
  test_keepalive_retries_a_refused_ping();
  test_keepalive_waits_before_asking_the_client_again();
  test_keepalive_counts_the_pings_the_client_refused();
  test_keepalive_gives_up_on_a_client_that_never_takes_the_ping();
  test_keepalive_starts_the_refusal_deadline_over_on_reconnect();

  FINISH
}
