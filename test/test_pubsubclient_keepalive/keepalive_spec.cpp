#include "PubSubClient.h"
#include "ShimClient.h"
#include "Buffer.h"
#include "BDDTest.h"
#include "trace.h"
#include "Arduino.h"
#include <cstring>

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
  constexpr uint32_t stepMs = 250U;   // Finer step, for the tests that watch where inside an interval something lands.
}  // namespace

bool message_arrived = false;
char arrivedTopic[256] = { '\0' };

void callback(char* topic, [[maybe_unused]] uint8_t* payload, [[maybe_unused]] unsigned int length) {
  message_arrived = true;
  strncpy(arrivedTopic, topic, sizeof(arrivedTopic) - 1U);
  arrivedTopic[sizeof(arrivedTopic) - 1U] = '\0';
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
  IT("gives up on a hung connection before the broker stops waiting");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  // The shape this is meant to be run in: the broker is told to wait a keep-alive interval, and
  // the ping falls due well inside it, so what is left is room to ask again.
  client.setKeepAlive(15U).setPingInterval(5U);
  IS_TRUE(client.connect("client_test1"));
  const uint16_t afterConnect = shimClient.received();

  // Nothing is ever answered. A broker stops waiting one and a half intervals after the last
  // packet it saw - the connect, here - so the session has to end before that, and end here.
  const uint32_t brokerGivesUpMs = baseMs + ((3U * 15U * tickMs) / 2U);
  bool rc = true;
  uint32_t now = baseMs;
  while(rc && (now < brokerGivesUpMs)) {
    now += stepMs;
    setFakeMillis(now);
    rc = client.loop();
  }

  IS_FALSE(rc);
  IS_TRUE(now < brokerGivesUpMs);
  IS_TRUE(client.state() == PubSubClient::State::CONNECTION_TIMEOUT);
  // And it asked more than once on the way there.
  IS_TRUE(shimClient.received() > static_cast<uint16_t>(afterConnect + 2U));

  IS_FALSE(shimClient.error());

  clearFakeMillis();
  END_IT
}

bool test_keepalive_asks_again_for_a_missing_ping_answer() {
  IT("asks again for a ping answer that never came, and keeps the session when it does");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  client.setKeepAlive(15U).setPingInterval(5U);
  IS_TRUE(client.connect("client_test1"));
  const uint16_t afterConnect = shimClient.received();

  // A ping interval after the connect the ping goes out, and nothing answers it.
  setFakeMillis(baseMs + (6U * tickMs));
  IS_TRUE(client.loop());
  IS_EQUAL(shimClient.received(), static_cast<uint16_t>(afterConnect + 2U));

  // A second later, with nothing waiting to be read, it is asked again.
  setFakeMillis(baseMs + (7U * tickMs));
  IS_TRUE(client.loop());
  IS_EQUAL(shimClient.received(), static_cast<uint16_t>(afterConnect + 4U));

  // This one is answered.
  const uint8_t pingresp[] = { 0xD0U, 0x0U };
  shimClient.respond(pingresp, 2U);
  setFakeMillis(baseMs + (7U * tickMs) + stepMs);
  IS_TRUE(client.loop());

  // Past the point the broker would have stopped waiting on the ping that went missing, the
  // session is still up.
  setFakeMillis(baseMs + (23U * tickMs));
  IS_TRUE(client.loop());
  IS_TRUE(client.connected());

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
  IT("reports a timeout when the client refuses the ping for longer than it has");

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

  // The time it has is the same the broker leaves for a ping to be answered in: two fifths of an
  // interval here, the ping interval being the keep-alive by default, counted from the moment the
  // ping fell due rather than from the pass that noticed.
  setFakeMillis(baseMs + (20U * tickMs));
  IS_TRUE(client.loop());
  setFakeMillis(baseMs + (21U * tickMs));
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

bool test_keepalive_a_refused_publish_is_not_traffic() {
  IT("a publish the link would not take does not put the ping off");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  // Something arrives, so only the outgoing side of the keep-alive is left to drive the ping.
  const uint8_t publish[] = { 0x30U, 0xeU, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  setFakeMillis(baseMs + (9U * tickMs));
  shimClient.respond(publish, 16U);
  IS_TRUE(client.loop());

  setFakeMillis(baseMs + (11U * tickMs));
  shimClient.failNextWrites(1U);
  IS_FALSE(client.publish("topic", "payload"));

  // A keep-alive interval after the last packet that did go out, the ping is due.
  setFakeMillis(baseMs + (16U * tickMs));
  const uint16_t beforePing = shimClient.received();
  const uint8_t pingreq[] = { 0xC0U, 0x0U };
  shimClient.expect(pingreq, 2U);
  const uint8_t pingresp[] = { 0xD0U, 0x0U };
  shimClient.respond(pingresp, 2U);
  IS_TRUE(client.loop());
  IS_EQUAL(shimClient.received(), static_cast<uint16_t>(beforePing + 2U));

  clearFakeMillis();
  END_IT
}

bool test_keepalive_a_refused_puback_is_not_traffic() {
  IT("an acknowledgement the link would not take does not put the ping off");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  IS_TRUE(client.connect("client_test1"));

  // A qos1 message arrives and the link refuses the acknowledgement it is answered with. Nothing
  // went out, so the last packet the broker saw is still the connect.
  const uint8_t publish[] = { 0x32U, 0x10U, 0x0U, 0x5U, 0x74U, 0x6fU, 0x70U, 0x69U, 0x63U, 0x12U, 0x34U, 0x70U, 0x61U, 0x79U, 0x6cU, 0x6fU, 0x61U, 0x64U };
  setFakeMillis(baseMs + (9U * tickMs));
  shimClient.respond(publish, 18U);
  shimClient.failNextWrites(1U);
  IS_TRUE(client.loop());

  // A keep-alive interval after the connect, the ping is due: the broker has heard nothing since.
  setFakeMillis(baseMs + (16U * tickMs));
  const uint16_t beforePing = shimClient.received();
  const uint8_t pingreq[] = { 0xC0U, 0x0U };
  shimClient.expect(pingreq, 2U);
  const uint8_t pingresp[] = { 0xD0U, 0x0U };
  shimClient.respond(pingresp, 2U);
  IS_TRUE(client.loop());
  IS_EQUAL(shimClient.received(), static_cast<uint16_t>(beforePing + 2U));

  IS_FALSE(shimClient.error());

  clearFakeMillis();
  END_IT
}

bool test_keepalive_holds_the_ping_inside_the_keepalive() {
  IT("holds a ping interval asked to go above the keep-alive down to it");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  client.setKeepAlive(10U).setPingInterval(30U);
  IS_TRUE(client.connect("client_test1"));
  const uint16_t afterConnect = shimClient.received();

  // Two pings fall due inside twenty-two seconds at the keep-alive's ten, and none at all at the
  // thirty seconds asked for.
  const uint8_t pingresp[] = { 0xD0U, 0x0U };
  setFakeMillis(baseMs + (11U * tickMs));
  IS_TRUE(client.loop());
  shimClient.respond(pingresp, 2U);
  setFakeMillis(baseMs + (11U * tickMs) + stepMs);
  IS_TRUE(client.loop());
  setFakeMillis(baseMs + (22U * tickMs));
  IS_TRUE(client.loop());
  IS_EQUAL(shimClient.received(), static_cast<uint16_t>(afterConnect + 4U));

  IS_FALSE(shimClient.error());

  clearFakeMillis();
  END_IT
}

bool test_keepalive_brings_the_ping_down_with_the_keepalive() {
  IT("brings a ping interval down with a keep-alive set under it afterwards");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  // Room for a twenty-second ping when it is set, and none for it afterwards.
  client.setKeepAlive(30U).setPingInterval(20U).setKeepAlive(10U);
  IS_TRUE(client.connect("client_test1"));
  const uint16_t afterConnect = shimClient.received();

  // Ten seconds is what the ping runs at now, not the twenty it was given: two fall due inside
  // twenty-two seconds, where twenty would have left room for one.
  const uint8_t pingresp[] = { 0xD0U, 0x0U };
  setFakeMillis(baseMs + (11U * tickMs));
  IS_TRUE(client.loop());
  shimClient.respond(pingresp, 2U);
  setFakeMillis(baseMs + (11U * tickMs) + stepMs);
  IS_TRUE(client.loop());
  setFakeMillis(baseMs + (22U * tickMs));
  IS_TRUE(client.loop());
  IS_EQUAL(shimClient.received(), static_cast<uint16_t>(afterConnect + 4U));

  IS_FALSE(shimClient.error());

  clearFakeMillis();
  END_IT
}

bool test_keepalive_gives_the_ping_interval_back_when_the_keepalive_is_raised() {
  IT("gives a capped ping interval back when the keep-alive is raised over it again");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  // Twenty seconds asked for, squeezed to five by the keep-alive, then room made for it again.
  client.setKeepAlive(30U).setPingInterval(20U).setKeepAlive(5U).setKeepAlive(30U);
  IS_TRUE(client.connect("client_test1"));

  // The next bytes the link may see are this publish's. A ping interval left behind at five would
  // have one falling due before the pass below, and it would land on the expectation first.
  const uint8_t expected[] = { 0x30U, 0x8U, 0x0U, 0x4U, 0x67U, 0x6fU, 0x6fU, 0x64U, 0x31U, 0x32U };
  shimClient.expect(expected, 10U);
  setFakeMillis(baseMs + (11U * tickMs));
  IS_TRUE(client.loop());
  IS_TRUE(client.publish("good", "12"));

  // And the twenty seconds it was given are what the ping waits out.
  const uint8_t pingreq[] = { 0xC0U, 0x0U };
  shimClient.expect(pingreq, 2U);
  setFakeMillis(baseMs + (22U * tickMs));
  IS_TRUE(client.loop());

  IS_FALSE(shimClient.error());

  clearFakeMillis();
  END_IT
}

bool test_keepalive_counts_the_answers_that_went_missing() {
  IT("counts one per ping whose answer went missing, not one per ask");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  client.setKeepAlive(15U).setPingInterval(5U);
  IS_TRUE(client.connect("client_test1"));
  IS_EQUAL(client.getUnansweredPingCount(), 0U);

  // The ping goes out; nothing has gone missing until it is asked for a second time.
  setFakeMillis(baseMs + (6U * tickMs));
  IS_TRUE(client.loop());
  IS_EQUAL(client.getUnansweredPingCount(), 0U);

  setFakeMillis(baseMs + (7U * tickMs));
  IS_TRUE(client.loop());
  IS_EQUAL(client.getUnansweredPingCount(), 1U);

  // The asks after it are the same ping again.
  setFakeMillis(baseMs + (8U * tickMs));
  IS_TRUE(client.loop());
  IS_EQUAL(client.getUnansweredPingCount(), 1U);

  const uint8_t pingresp[] = { 0xD0U, 0x0U };
  shimClient.respond(pingresp, 2U);
  setFakeMillis(baseMs + (8U * tickMs) + stepMs);
  IS_TRUE(client.loop());

  // A second ping, a second answer that never comes, and the count moves once more.
  setFakeMillis(baseMs + (14U * tickMs));
  IS_TRUE(client.loop());
  setFakeMillis(baseMs + (15U * tickMs));
  IS_TRUE(client.loop());
  IS_EQUAL(client.getUnansweredPingCount(), 2U);

  IS_FALSE(shimClient.error());

  clearFakeMillis();
  END_IT
}

bool test_keepalive_one_deadline_covers_a_ping_refused_then_taken() {
  IT("gives up inside the broker's patience when the ping is refused before it is taken");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  (void)client.setKeepAlive(15U);
  (void)client.setPingInterval(5U);
  IS_TRUE(client.connect("client_test1"));

  // The broker stops waiting three halves of a keep-alive after the CONNECT, the last thing it
  // heard. Refusals must not buy the session time past that: they share the one deadline the run
  // has from the moment the ping fell due.
  const uint32_t brokerGivesUpAt = baseMs + 22500U;
  shimClient.failNextWrites(5U);

  uint32_t gaveUpAt = 0U;
  for(uint32_t t = baseMs + 100U; (t < brokerGivesUpAt) && (gaveUpAt == 0U); t += 100U) {
    setFakeMillis(t);
    if(!client.loop()) { gaveUpAt = t; }
  }

  IS_TRUE(gaveUpAt != 0U);
  IS_TRUE(client.state() == PubSubClient::State::CONNECTION_TIMEOUT);
  // The ping falls due one ping interval after the last traffic and the run lasts seven fifths of
  // a keep-alive from there, whichever attempt is outstanding when the time runs out.
  IS_EQUAL(gaveUpAt, baseMs + 21000U);
  IS_TRUE(client.getRefusedPingCount() == 1U);

  clearFakeMillis();
  END_IT
}

bool test_keepalive_a_late_loop_does_not_move_the_deadline() {
  IT("gives up inside the broker's patience even when loop() comes back late");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  (void)client.setKeepAlive(15U);
  (void)client.setPingInterval(5U);
  IS_TRUE(client.connect("client_test1"));

  // Three seconds pass between the ping falling due and the pass that gets to notice - a flash
  // write holding the main loop is enough - and the client will not take the ping when it runs.
  const uint32_t brokerGivesUpAt = baseMs + 22500U;
  shimClient.failNextWrites(200U);

  uint32_t gaveUpAt = 0U;
  for(uint32_t t = baseMs + 8000U; (t < brokerGivesUpAt) && (gaveUpAt == 0U); t += 100U) {
    setFakeMillis(t);
    if(!client.loop()) { gaveUpAt = t; }
  }

  IS_TRUE(gaveUpAt != 0U);
  IS_TRUE(client.state() == PubSubClient::State::CONNECTION_TIMEOUT);
  // The same moment a loop() running on time would have reached: the three seconds it was away
  // come out of the run, not off the front of it.
  IS_EQUAL(gaveUpAt, baseMs + 21000U);

  clearFakeMillis();
  END_IT
}

bool test_keepalive_zero_leaves_the_session_alone() {
  IT("never pings and never times out when the keep-alive is zero");

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  // Zero is how the broker is told not to time this client out, so there is nothing to prove.
  (void)client.setKeepAlive(0U);
  IS_TRUE(client.connect("client_test1"));

  // The next bytes the link may see are this publish's, two minutes of silence later: a ping sent
  // in between would land on the expectation first and be caught as a mismatch.
  const uint8_t expected[] = { 0x30U, 0x8U, 0x0U, 0x4U, 0x67U, 0x6fU, 0x6fU, 0x64U, 0x31U, 0x32U };
  shimClient.expect(expected, 10U);

  for(uint32_t t = baseMs + tickMs; t <= (baseMs + (120U * tickMs)); t += tickMs) {
    setFakeMillis(t);
    IS_TRUE(client.loop());
  }

  IS_TRUE(client.publish("good", "12"));
  IS_FALSE(shimClient.error());
  IS_TRUE(client.state() == PubSubClient::State::CONNECTED);

  clearFakeMillis();
  END_IT
}

bool test_keepalive_ping_leaves_a_part_read_message_alone() {
  IT("delivers a message the ping fell due in the middle of");
  message_arrived = false;
  arrivedTopic[0] = '\0';

  ShimClient shimClient;
  shimClient.setAllowConnect(true);

  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, 4U);

  setFakeMillis(baseMs);
  PubSubClient client(server, 1883U, callback, shimClient);
  (void)client.setKeepAlive(15U);
  (void)client.setPingInterval(5U);
  IS_TRUE(client.connect("client_test1"));

  // Just under a ping interval after the connect, the first 8 bytes of a 14-byte PUBLISH to
  // "topic" arrive. Nothing has completed since, so the link still reads as quiet.
  setFakeMillis(baseMs + 4900U);
  const uint8_t head[] = { 0x30U, 0x0CU, 0x00U, 0x05U, 0x74U, 0x6fU, 0x70U, 0x69U };
  shimClient.respond(head, 8U);
  IS_TRUE(client.loop());
  IS_FALSE(message_arrived);

  // The ping falls due here, with the message still part-read. Two bytes written over its header
  // would have the rest of it delivered as whatever those bytes now spell.
  setFakeMillis(baseMs + 5100U);
  IS_TRUE(client.loop());

  setFakeMillis(baseMs + 5300U);
  const uint8_t tail[] = { 0x63U, 0x68U, 0x65U, 0x6cU, 0x6cU, 0x6fU };
  shimClient.respond(tail, 6U);
  const uint8_t pingresp[] = { 0xD0U, 0x00U };
  shimClient.respond(pingresp, 2U);
  IS_TRUE(client.loop());

  IS_TRUE(message_arrived);
  IS_TRUE(strcmp(arrivedTopic, "topic") == 0);
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
  test_keepalive_asks_again_for_a_missing_ping_answer();
  test_keepalive_holds_the_ping_inside_the_keepalive();
  test_keepalive_brings_the_ping_down_with_the_keepalive();
  test_keepalive_retries_a_refused_ping();
  test_keepalive_waits_before_asking_the_client_again();
  test_keepalive_counts_the_pings_the_client_refused();
  test_keepalive_gives_the_ping_interval_back_when_the_keepalive_is_raised();
  test_keepalive_counts_the_answers_that_went_missing();
  test_keepalive_gives_up_on_a_client_that_never_takes_the_ping();
  test_keepalive_starts_the_refusal_deadline_over_on_reconnect();
  test_keepalive_a_refused_publish_is_not_traffic();
  test_keepalive_a_refused_puback_is_not_traffic();
  test_keepalive_one_deadline_covers_a_ping_refused_then_taken();
  test_keepalive_a_late_loop_does_not_move_the_deadline();
  test_keepalive_zero_leaves_the_session_alone();
  test_keepalive_ping_leaves_a_part_read_message_alone();

  FINISH
}
