#include "messageRoute.hpp"
#include "BDDTest.h"
#include <string.h>

// The routing decision Connectivity's delivery makes: which module a message belongs to, and
// what that module should be shown. Connectivity itself needs the whole MQTT stack, so this is
// the part the host can reach - and it is the part that decides where a command ends up.

/// @brief Parses a payload and resolves it as if it had arrived on `arrivedOn`.
/// @details The document has to outlive the route, which holds views into it, so each test keeps
/// its own and passes it in.
static MessageRoute::Route routeOf(JsonDocument& doc, const char* arrivedOn, const char* json) {
  (void)deserializeJson(doc, json);
  return MessageRoute::resolve(arrivedOn, doc);
}

// ---- no envelope: delivered where it arrived ----

bool test_a_plain_message_stays_where_it_arrived() {
  IT("a message with no envelope goes to the subtopic it was published to");
  JsonDocument doc;
  const MessageRoute::Route route = routeOf(doc, "alert1", R"({"Sound":300,"Volume":20})");
  IS_TRUE(route.target != nullptr);
  IS_TRUE(strcmp(route.target, "alert1") == 0);
  IS_FALSE(route.rerouted);
  IS_EQUAL(route.body["Sound"].as<uint16_t>(), 300U);
  END_IT
}

bool test_a_non_string_to_is_not_an_envelope() {
  IT("a 'to' that is not a string leaves the message where it arrived");
  // Otherwise a module could never use 'to' as an ordinary key of its own.
  JsonDocument doc;
  const MessageRoute::Route route = routeOf(doc, "alert1", R"({"to":42,"Sound":1})");
  IS_TRUE(route.target != nullptr);
  IS_TRUE(strcmp(route.target, "alert1") == 0);
  IS_FALSE(route.rerouted);
  END_IT
}

// ---- envelope: delivered elsewhere ----

bool test_an_envelope_delivers_the_inner_object() {
  IT("an envelope hands the inner object to the subtopic it names");
  JsonDocument doc;
  const MessageRoute::Route route = routeOf(doc, "common", R"({"to":"alert1","msg":{"setCanId":28}})");
  IS_TRUE(route.target != nullptr);
  IS_TRUE(strcmp(route.target, "alert1") == 0);
  IS_TRUE(route.rerouted);
  IS_EQUAL(route.body["setCanId"].as<uint16_t>(), 28U);
  // The envelope's own keys must not be visible to the handler.
  IS_FALSE(route.body["to"].is<const char*>());
  END_IT
}

bool test_the_inner_object_is_a_view_not_a_copy() {
  IT("the body points into the message, so nothing is duplicated to deliver it");
  JsonDocument doc;
  const MessageRoute::Route route = routeOf(doc, "common", R"({"to":"alert1","msg":{"Volume":7}})");
  doc["msg"]["Volume"] = 9;
  IS_EQUAL(route.body["Volume"].as<uint16_t>(), 9U);
  END_IT
}

// ---- envelopes that are refused ----

bool test_an_envelope_is_only_read_on_the_device_wide_subtopic() {
  IT("an envelope that arrived on a module's own subtopic is refused");
  // Otherwise publish rights to one module would be publish rights to all of them, and a broker
  // ACL naming a subtopic would stop isolating anything.
  JsonDocument doc;
  const MessageRoute::Route route = routeOf(doc, "alert1", R"({"to":"alert2","msg":{"Volume":7}})");
  IS_TRUE(route.target == nullptr);
  END_IT
}

bool test_the_device_wide_subtopic_is_the_one_that_routes() {
  IT("the subtopic envelopes are read on is the one the common handler runs on");
  // The router and the handler have to agree on the name, so both take it from here.
  JsonDocument doc;
  const MessageRoute::Route route = routeOf(doc, MqttTopics::getCommonSubtopic(),
                                            R"({"to":"alert1","msg":{"Volume":7}})");
  IS_TRUE(route.target != nullptr);
  IS_TRUE(route.rerouted);
  END_IT
}

bool test_a_plain_message_is_untouched_on_any_subtopic() {
  IT("a message without an envelope is delivered wherever it arrived");
  // The restriction is on routing, not on receiving: a module's own subtopic still works.
  JsonDocument doc;
  const MessageRoute::Route route = routeOf(doc, "alert1", R"({"Volume":7,"to":42})");
  IS_TRUE(route.target != nullptr);
  IS_TRUE(strcmp(route.target, "alert1") == 0);
  IS_FALSE(route.rerouted);
  END_IT
}

bool test_an_envelope_naming_its_own_subtopic_is_refused() {
  IT("an envelope addressed to the subtopic it arrived on is refused");
  // It wraps nothing, so it is a mistake rather than an instruction - and refusing it is what
  // keeps the single unwrap from ever needing a second one.
  JsonDocument doc;
  const MessageRoute::Route route = routeOf(doc, "common", R"({"to":"common","msg":{"cmd":"reboot"}})");
  IS_TRUE(route.target == nullptr);
  END_IT
}

bool test_an_envelope_without_a_body_is_refused() {
  IT("an envelope with no 'msg' object is refused");
  JsonDocument doc;
  IS_TRUE(routeOf(doc, "common", R"({"to":"alert1"})").target == nullptr);
  JsonDocument second;
  IS_TRUE(routeOf(second, "common", R"({"to":"alert1","msg":5})").target == nullptr);
  JsonDocument third;
  IS_TRUE(routeOf(third, "common", R"({"to":"alert1","msg":[1,2]})").target == nullptr);
  END_IT
}

bool test_an_empty_target_is_refused() {
  IT("an envelope naming an empty subtopic is refused");
  JsonDocument doc;
  IS_TRUE(routeOf(doc, "common", R"({"to":"","msg":{"cmd":"reboot"}})").target == nullptr);
  END_IT
}

bool test_a_null_arrival_subtopic_is_refused() {
  IT("a message with no arrival subtopic resolves to nothing");
  JsonDocument doc;
  (void)deserializeJson(doc, R"({"cmd":"reboot"})");
  IS_TRUE(MessageRoute::resolve(nullptr, doc).target == nullptr);
  END_IT
}

// ---- the envelope is read once ----

bool test_a_nested_envelope_is_not_opened_twice() {
  IT("an envelope inside an envelope is delivered as a body, not followed");
  // The result is never resolved again, so the inner 'to' is just a key the handler sees. This
  // is what makes a message unable to circle between subtopics.
  JsonDocument doc;
  const MessageRoute::Route route = routeOf(doc, "common", R"({"to":"alert1","msg":{"to":"common","msg":{"cmd":"reboot"}}})");
  IS_TRUE(route.target != nullptr);
  IS_TRUE(strcmp(route.target, "alert1") == 0);
  IS_TRUE(route.body["to"].is<const char*>());
  END_IT
}

int main() {
  SUITE("MessageRoute");
  test_a_plain_message_stays_where_it_arrived();
  test_a_non_string_to_is_not_an_envelope();
  test_an_envelope_delivers_the_inner_object();
  test_the_inner_object_is_a_view_not_a_copy();
  test_an_envelope_is_only_read_on_the_device_wide_subtopic();
  test_the_device_wide_subtopic_is_the_one_that_routes();
  test_a_plain_message_is_untouched_on_any_subtopic();
  test_an_envelope_naming_its_own_subtopic_is_refused();
  test_an_envelope_without_a_body_is_refused();
  test_an_empty_target_is_refused();
  test_a_null_arrival_subtopic_is_refused();
  test_a_nested_envelope_is_not_opened_twice();
  FINISH
}
