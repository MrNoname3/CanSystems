#include "canIdAssign.hpp"
#include "BDDTest.h"

// The node side of SET_CAN_ID lives in canHandlerAtmega328P, which the native build excludes
// (#ifdef ARDUINO_ARCH_AVR). The decision it makes is in these helpers instead, so what the AVR
// firmware would obey - and refuse - is exercised here.

static constexpr uint16_t masterId = 10U;
static constexpr uint16_t nodeId = 26U;

/// @brief A well-formed request from the master, as the gateway would pack it.
static CanIdAssign::Request goodRequest(uint16_t newLocal) {
  CanIdAssign::Request request;
  request.expectedLocal = nodeId;
  request.newLocal = newLocal;
  request.reservedClear = true;
  return request;
}

// ---- byte layout (the contract, pinned) ----

bool test_byte_layout() {
  IT("pack lays the fields out little-endian and zeroes the unused bytes");
  CanIdAssign::Request fields;
  fields.expectedLocal = 0x0201U;
  fields.newLocal = 0x0403U;
  uint8_t canData[8] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };
  CanIdAssign::pack(fields, canData);
  IS_EQUAL(canData[0], 0x01U);                       // expectedLocal low
  IS_EQUAL(canData[1], 0x02U);                       // expectedLocal high
  IS_EQUAL(canData[2], 0x03U);                       // newLocal low
  IS_EQUAL(canData[3], 0x04U);                       // newLocal high
  IS_EQUAL(canData[4], 0x00U);
  IS_EQUAL(canData[5], 0x00U);
  IS_EQUAL(canData[6], 0x00U);
  IS_EQUAL(canData[7], 0x00U);
  END_IT
}

bool test_round_trip() {
  IT("a packed request unpacks into the same fields");
  const CanIdAssign::Request sent = goodRequest(28U);
  uint8_t canData[8] = { 0U };
  CanIdAssign::pack(sent, canData);
  const CanIdAssign::Request received = CanIdAssign::unpack(canData);
  IS_EQUAL(received.expectedLocal, sent.expectedLocal);
  IS_EQUAL(received.newLocal, sent.newLocal);
  IS_TRUE(received.reservedClear);
  END_IT
}

bool test_dirty_reserved_bytes_are_reported() {
  IT("unpack reports reserved bytes that did not arrive zero");
  uint8_t canData[8] = { 0U };
  CanIdAssign::pack(goodRequest(28U), canData);
  canData[6] = 1U;
  IS_FALSE(CanIdAssign::unpack(canData).reservedClear);
  END_IT
}

// ---- which ids may be handed out ----

bool test_assignable_ids() {
  IT("zero and anything past the 10-bit field are not addresses");
  // Zero is what an unset address reads as, so it can never be handed out on purpose.
  IS_FALSE(CanIdAssign::isAssignableId(0U));
  IS_TRUE(CanIdAssign::isAssignableId(1U));
  IS_TRUE(CanIdAssign::isAssignableId(CanIdAssign::idMask));
  IS_FALSE(CanIdAssign::isAssignableId(CanIdAssign::idMask + 1U));
  IS_FALSE(CanIdAssign::isAssignableId(0xFFFFU));
  END_IT
}

// ---- what a node obeys ----

bool test_a_well_formed_request_from_the_master_is_obeyed() {
  IT("a request from the master, naming the node's current id, is accepted");
  IS_TRUE(CanIdAssign::isAcceptable(goodRequest(28U), nodeId, masterId, masterId));
  END_IT
}

bool test_only_the_master_may_renumber() {
  IT("a request from anyone but the master is refused");
  // The receive filter matches on the addressee alone, so without this check any node on the
  // bus could renumber any other.
  IS_FALSE(CanIdAssign::isAcceptable(goodRequest(28U), nodeId, masterId, 27U));
  IS_FALSE(CanIdAssign::isAcceptable(goodRequest(28U), nodeId, masterId, nodeId));
  END_IT
}

bool test_a_request_naming_another_id_is_refused() {
  IT("a request that names an id this node does not hold is refused");
  // A repeat that arrives after the change would otherwise be obeyed by whichever node has
  // since taken the id it names.
  CanIdAssign::Request request = goodRequest(28U);
  request.expectedLocal = 27U;
  IS_FALSE(CanIdAssign::isAcceptable(request, nodeId, masterId, masterId));
  END_IT
}

bool test_dirty_reserved_bytes_are_refused() {
  IT("a request whose reserved bytes are not zero is refused");
  CanIdAssign::Request request = goodRequest(28U);
  request.reservedClear = false;
  IS_FALSE(CanIdAssign::isAcceptable(request, nodeId, masterId, masterId));
  END_IT
}

bool test_an_unusable_new_id_is_refused() {
  IT("a new id of zero or past the 10-bit field is refused");
  IS_FALSE(CanIdAssign::isAcceptable(goodRequest(0U), nodeId, masterId, masterId));
  IS_FALSE(CanIdAssign::isAcceptable(goodRequest(CanIdAssign::idMask + 1U), nodeId, masterId, masterId));
  END_IT
}

bool test_the_master_id_is_refused() {
  IT("a node is not given the master's id");
  // It would end up filtering on the address it sends its answers to.
  IS_FALSE(CanIdAssign::isAcceptable(goodRequest(masterId), nodeId, masterId, masterId));
  END_IT
}

bool test_the_id_the_node_already_holds_is_accepted() {
  IT("being told to keep the id it already holds is a valid request");
  // Harmless and idempotent: the node stores the same value and restarts.
  IS_TRUE(CanIdAssign::isAcceptable(goodRequest(nodeId), nodeId, masterId, masterId));
  END_IT
}

int main() {
  SUITE("CanIdAssign");
  test_byte_layout();
  test_round_trip();
  test_dirty_reserved_bytes_are_reported();
  test_assignable_ids();
  test_a_well_formed_request_from_the_master_is_obeyed();
  test_only_the_master_may_renumber();
  test_a_request_naming_another_id_is_refused();
  test_dirty_reserved_bytes_are_refused();
  test_an_unusable_new_id_is_refused();
  test_the_master_id_is_refused();
  test_the_id_the_node_already_holds_is_accepted();
  FINISH
}
