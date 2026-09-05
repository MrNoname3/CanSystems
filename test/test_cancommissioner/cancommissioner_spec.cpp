#include "canCommissioner.hpp"
#include "testCan.hpp"
#include "esp32CanModel.h"
#include "EEPROM.h"
#include "crc16.hpp"
#include "Arduino.h"
#include "BDDTest.h"
#include <string>

// The gateway side of giving a node an address: what it does with an announcement no driver
// claims, and what it sends when told to hand an address out.

static constexpr uint16_t kMasterId = 10U;
static constexpr uint16_t kLocalId = 10U;
static const char* kUidHex = "1122334455667788";
static const uint8_t kUid[8] = { 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U };

/// @brief The layout EEPROMHandler<CanId, 0> stores, so the handler's init() finds ids it accepts.
static void seedCanIds(uint16_t master, uint16_t local) {
  struct __attribute__((packed)) StoredIds {
    uint16_t crc;
    // cppcheck-suppress unusedStructMember ; read back through EEPROM.put's byte copy
    uint16_t master;
    // cppcheck-suppress unusedStructMember ; read back through EEPROM.put's byte copy
    uint16_t local;
  };
  StoredIds stored{ 0U, master, local };
  stored.crc = Crc16::calculate(reinterpret_cast<uint8_t*>(&stored), sizeof(stored));
  EEPROM.put(0U, stored);
}

static void resetEnv() {
  EEPROM.clear();
  seedCanIds(kMasterId, kLocalId);
  esp32Can.reset();
  MqttBase::resetState();
  setFakeMillis(0U);
}

static uint32_t extIdOf(uint16_t to, uint16_t cmd, uint16_t from) {
  return (static_cast<uint32_t>(to) & 0x3FFU) | ((static_cast<uint32_t>(cmd) & 0x1FFU) << 10U) |
         ((static_cast<uint32_t>(from) & 0x3FFU) << 19U);
}

/// @brief Puts one frame on the modelled bus and lets the handler drain it.
static void deliverFrame(CanHandler& handler, uint16_t from, uint16_t cmd, const uint8_t (&payload)[8]) {
  esp32Can.queueExtendedFrame(extIdOf(kLocalId, cmd, from), payload, 8U);
  esp32TriggerCanInterrupt();
  (void)handler.run();
}

/// @brief An announcement from a node waiting on the address its unique id derives.
static void announce(CanHandler& handler, const uint8_t (&uid)[8]) {
  uint8_t payload[8] = { 0U };
  memcpy(payload, uid, sizeof(payload));
  deliverFrame(handler, CanIdAssign::provisionalId(uid), static_cast<uint16_t>(CanCmd::ANNOUNCE), payload);
}

static void deliver(CanCommissioner& commissioner, const char* json) {
  MqttBase& mqttSide = commissioner;
  JsonDocument doc;
  (void)deserializeJson(doc, json);
  mqttSide.messageArrivedCallback(doc);
}

// ---- hearing a node ----

bool test_an_announcement_is_noticed() {
  IT("a node announcing itself turns up in the list");
  resetEnv();
  TestCan can;
  Connectivity conn;
  CanCommissioner commissioner(can, conn, "can");
  Task& canTask = can.handler;
  Task& task = commissioner;
  IS_TRUE(canTask.init());
  IS_TRUE(task.init());

  announce(can.handler, kUid);
  deliver(commissioner, R"({"list":true})");
  IS_TRUE(MqttBase::lastReply.find(kUidHex) != std::string::npos);
  IS_TRUE(MqttBase::lastReply.find(std::to_string(CanIdAssign::provisionalId(kUid))) != std::string::npos);
  END_IT
}

bool test_nothing_waiting_is_an_empty_list() {
  IT("with nothing waiting the list is empty");
  resetEnv();
  TestCan can;
  Connectivity conn;
  CanCommissioner commissioner(can, conn, "can");
  Task& task = commissioner;
  IS_TRUE(task.init());

  deliver(commissioner, R"({"list":true})");
  IS_TRUE(MqttBase::lastReply == R"({"waiting":[]})");
  END_IT
}

bool test_the_same_node_is_listed_once() {
  IT("a node announcing again does not appear twice");
  resetEnv();
  TestCan can;
  Connectivity conn;
  CanCommissioner commissioner(can, conn, "can");
  Task& canTask = can.handler;
  Task& task = commissioner;
  IS_TRUE(canTask.init());
  IS_TRUE(task.init());

  announce(can.handler, kUid);
  announce(can.handler, kUid);
  deliver(commissioner, R"({"list":true})");
  const size_t first = MqttBase::lastReply.find(kUidHex);
  IS_TRUE(first != std::string::npos);
  IS_TRUE(MqttBase::lastReply.find(kUidHex, first + 1U) == std::string::npos);
  END_IT
}

bool test_a_frame_that_is_not_an_announcement_is_ignored() {
  IT("an unclaimed frame that is not an announcement is not taken for one");
  resetEnv();
  TestCan can;
  Connectivity conn;
  CanCommissioner commissioner(can, conn, "can");
  Task& canTask = can.handler;
  Task& task = commissioner;
  IS_TRUE(canTask.init());
  IS_TRUE(task.init());

  uint8_t payload[8] = { 0U };
  memcpy(payload, kUid, sizeof(payload));
  deliverFrame(can.handler, CanIdAssign::provisionalId(kUid), static_cast<uint16_t>(CanCmd::PING), payload);
  deliver(commissioner, R"({"list":true})");
  IS_TRUE(MqttBase::lastReply == R"({"waiting":[]})");
  END_IT
}

bool test_an_announcement_from_a_real_address_is_ignored() {
  IT("an announcement from outside the provisional block is not taken for a waiting node");
  // Only a node that gave itself an address is waiting for one; anything else already has one.
  resetEnv();
  TestCan can;
  Connectivity conn;
  CanCommissioner commissioner(can, conn, "can");
  Task& canTask = can.handler;
  Task& task = commissioner;
  IS_TRUE(canTask.init());
  IS_TRUE(task.init());

  uint8_t payload[8] = { 0U };
  memcpy(payload, kUid, sizeof(payload));
  deliverFrame(can.handler, 26U, static_cast<uint16_t>(CanCmd::ANNOUNCE), payload);
  deliver(commissioner, R"({"list":true})");
  IS_TRUE(MqttBase::lastReply == R"({"waiting":[]})");
  END_IT
}

bool test_a_silent_node_is_forgotten() {
  IT("a node that stops announcing is taken off the list");
  resetEnv();
  TestCan can;
  Connectivity conn;
  CanCommissioner commissioner(can, conn, "can");
  Task& canTask = can.handler;
  Task& task = commissioner;
  IS_TRUE(canTask.init());
  IS_TRUE(task.init());

  announce(can.handler, kUid);
  setFakeMillis(31000U);
  IS_TRUE(task.run());
  deliver(commissioner, R"({"list":true})");
  IS_TRUE(MqttBase::lastReply == R"({"waiting":[]})");
  END_IT
}

bool test_a_full_list_drops_the_one_heard_from_longest_ago() {
  IT("a full list drops the node heard from longest ago, across the millis() wrap");
  resetEnv();
  TestCan can;
  Connectivity conn;
  CanCommissioner commissioner(can, conn, "can");
  Task& canTask = can.handler;
  Task& task = commissioner;
  IS_TRUE(canTask.init());
  IS_TRUE(task.init());

  static const uint8_t uidA[8] = { 0xA0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
  static const uint8_t uidB[8] = { 0xB0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
  static const uint8_t uidC[8] = { 0xC0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
  static const uint8_t uidD[8] = { 0xD0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
  static const uint8_t uidE[8] = { 0xE0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };

  setFakeMillis(0xFFFFFFF0U);           // heard first, just before the counter wraps
  announce(can.handler, uidA);
  setFakeMillis(0x00000010U);           // the rest arrive after it wrapped, so they read smaller
  announce(can.handler, uidB);
  setFakeMillis(0x00000014U);
  announce(can.handler, uidC);
  setFakeMillis(0x00000018U);
  announce(can.handler, uidD);
  setFakeMillis(0x00000020U);
  announce(can.handler, uidE);          // full, and none of them is this node

  deliver(commissioner, R"({"list":true})");
  IS_TRUE(MqttBase::lastReply.find("a001020304050607") == std::string::npos);   // made way
  IS_TRUE(MqttBase::lastReply.find("b001020304050607") != std::string::npos);
  IS_TRUE(MqttBase::lastReply.find("e001020304050607") != std::string::npos);
  END_IT
}

// ---- handing an address out ----

bool test_assigning_sends_the_request_to_the_provisional_address() {
  IT("assigning an address sends SET_CAN_ID to where the node is answering");
  resetEnv();
  TestCan can;
  Connectivity conn;
  CanCommissioner commissioner(can, conn, "can");
  Task& canTask = can.handler;
  Task& task = commissioner;
  IS_TRUE(canTask.init());
  IS_TRUE(task.init());

  announce(can.handler, kUid);
  deliver(commissioner, R"({"assign":{"uid":"1122334455667788","id":28}})");
  const CanHandler::CanFrame* frame = lastCanFrame(static_cast<uint16_t>(CanCmd::SET_CAN_ID));
  IS_TRUE(frame != nullptr);
  if(frame != nullptr) {
    IS_EQUAL(static_cast<uint16_t>(frame->to), CanIdAssign::provisionalId(kUid));
    const CanIdAssign::Request request = CanIdAssign::unpack(frame->data);
    IS_EQUAL(request.expectedLocal, CanIdAssign::provisionalId(kUid));
    IS_EQUAL(request.newLocal, 28U);
  }
  IS_TRUE(MqttBase::lastResponse == MqttBase::Response::ACK);
  END_IT
}

bool test_an_unknown_node_is_refused() {
  IT("assigning to a unique id nothing announced is refused");
  resetEnv();
  TestCan can;
  Connectivity conn;
  CanCommissioner commissioner(can, conn, "can");
  Task& canTask = can.handler;
  Task& task = commissioner;
  IS_TRUE(canTask.init());
  IS_TRUE(task.init());

  announce(can.handler, kUid);
  deliver(commissioner, R"({"assign":{"uid":"aabbccddeeff0011","id":28}})");
  IS_EQUAL(countCanFrames(static_cast<uint16_t>(CanCmd::SET_CAN_ID)), 0U);
  IS_TRUE(MqttBase::lastResponse == MqttBase::Response::NACK);
  END_IT
}

bool test_a_malformed_unique_id_is_refused() {
  IT("a unique id that is not 16 hex characters is refused");
  resetEnv();
  TestCan can;
  Connectivity conn;
  CanCommissioner commissioner(can, conn, "can");
  Task& canTask = can.handler;
  Task& task = commissioner;
  IS_TRUE(canTask.init());
  IS_TRUE(task.init());

  announce(can.handler, kUid);
  deliver(commissioner, R"({"assign":{"uid":"11223344","id":28}})");
  deliver(commissioner, R"({"assign":{"uid":"11223344556677zz","id":28}})");
  deliver(commissioner, R"({"assign":{"uid":"11223344556677889","id":28}})");
  IS_EQUAL(countCanFrames(static_cast<uint16_t>(CanCmd::SET_CAN_ID)), 0U);
  END_IT
}

bool test_an_unusable_address_is_refused() {
  IT("an address that may not be handed out is refused before anything is sent");
  resetEnv();
  TestCan can;
  Connectivity conn;
  CanCommissioner commissioner(can, conn, "can");
  Task& canTask = can.handler;
  Task& task = commissioner;
  IS_TRUE(canTask.init());
  IS_TRUE(task.init());

  announce(can.handler, kUid);
  deliver(commissioner, R"({"assign":{"uid":"1122334455667788","id":0}})");
  // One from the provisional block would be indistinguishable from an address a node gave itself.
  deliver(commissioner, R"({"assign":{"uid":"1122334455667788","id":800}})");
  IS_EQUAL(countCanFrames(static_cast<uint16_t>(CanCmd::SET_CAN_ID)), 0U);
  IS_TRUE(MqttBase::lastResponse == MqttBase::Response::NACK);
  END_IT
}

int main() {
  SUITE("CanCommissioner");
  test_an_announcement_is_noticed();
  test_nothing_waiting_is_an_empty_list();
  test_the_same_node_is_listed_once();
  test_a_frame_that_is_not_an_announcement_is_ignored();
  test_an_announcement_from_a_real_address_is_ignored();
  test_a_silent_node_is_forgotten();
  test_a_full_list_drops_the_one_heard_from_longest_ago();
  test_assigning_sends_the_request_to_the_provisional_address();
  test_an_unknown_node_is_refused();
  test_a_malformed_unique_id_is_refused();
  test_an_unusable_address_is_refused();
  FINISH
}
