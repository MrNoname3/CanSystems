#include "MCP2515.h"
#include "canRxPump.hpp"
#include "Arduino.h"
#include "SPI.h"
#include "BDDTest.h"

// Characterisation of the MCP2515 receive path against the register-level model in
// test/_shims/src/SPI.h. The CAN handler's own RX pump is built on these guarantees:
// parsePacket() answering "nothing" is distinguishable, and one call drains one buffer.

static constexpr uint8_t kCsPin = 10U;
static constexpr uint8_t kIntPin = 2U;
static constexpr uint32_t kNoId = 0xFFFFFFFFU;

// Brings the controller up on the model. 8 MHz / 500 kbit/s matches the alert node's wiring.
static bool startController() {
  resetGpioState();
  mcp2515.reset();
  CAN.setPins(kCsPin, kIntPin);
  CAN.setClockFrequency(8000000U);
  return CAN.begin(500000U) == 1U;
}

bool test_begin_configures_the_controller() {
  IT("begin() leaves the controller in normal mode with both receive interrupts enabled");
  IS_TRUE(startController());
  IS_EQUAL(mcp2515.reg(0x0FU), 0x00U);                     // CANCTRL: normal mode
  IS_EQUAL(mcp2515.reg(0x2BU), 0x03U);                     // CANINTE: RX0IE | RX1IE
  END_IT
}

bool test_parse_packet_reports_nothing_when_no_frame_arrived() {
  IT("parsePacket() returns 0 and an invalid id when no receive flag is set");
  IS_TRUE(startController());
  IS_EQUAL(CAN.parsePacket(), 0U);
  IS_EQUAL(CAN.packetId(), kNoId);                         // the "nothing here" marker
  IS_EQUAL(CAN.packetDlc(), 0U);
  IS_EQUAL(CAN.available(), 0);
  END_IT
}

bool test_parse_packet_reads_an_extended_frame() {
  IT("parsePacket() decodes an extended frame and returns its length");
  IS_TRUE(startController());
  const uint8_t payload[4] = { 0xDEU, 0xADU, 0xBEU, 0xEFU };
  mcp2515.deliverExtendedFrame(0U, 0x12345678U, payload, 4U);
  IS_EQUAL(CAN.parsePacket(), 4U);
  IS_EQUAL(CAN.packetId(), 0x12345678U);
  IS_TRUE(CAN.packetExtended());
  IS_FALSE(CAN.packetRtr());
  uint8_t received[4] = { 0U };
  IS_EQUAL(CAN.readBytes(received, 4U), 4U);
  IS_EQUAL(received[0], 0xDEU);
  IS_EQUAL(received[3], 0xEFU);
  END_IT
}

bool test_parse_packet_drains_one_buffer_per_call() {
  IT("parsePacket() clears only the buffer it read, leaving the second frame pending");
  IS_TRUE(startController());
  const uint8_t first[1] = { 0x11U };
  const uint8_t second[1] = { 0x22U };
  mcp2515.deliverExtendedFrame(0U, 0x0000001AU, first, 1U);
  mcp2515.deliverExtendedFrame(1U, 0x0000002BU, second, 1U);
  IS_EQUAL(mcp2515.reg(Mcp2515Model::regCanIntf) & 0x03U, 0x03U);   // both RX flags raised

  IS_EQUAL(CAN.parsePacket(), 1U);
  IS_EQUAL(CAN.packetId(), 0x0000001AU);
  // RX0IF is cleared, RX1IF still stands: the controller is not drained by one call, and the
  // INT line therefore stays asserted - which is why a level trigger or a drain loop is needed.
  IS_EQUAL(mcp2515.reg(Mcp2515Model::regCanIntf) & 0x03U, 0x02U);

  IS_EQUAL(CAN.parsePacket(), 1U);
  IS_EQUAL(CAN.packetId(), 0x0000002BU);
  IS_EQUAL(mcp2515.reg(Mcp2515Model::regCanIntf) & 0x03U, 0x00U);
  END_IT
}

bool test_zero_length_frame_is_still_a_frame() {
  IT("a zero-length frame returns 0 from parsePacket() but reports a valid id");
  IS_TRUE(startController());
  mcp2515.deliverExtendedFrame(0U, 0x0000003CU, nullptr, 0U);
  IS_EQUAL(CAN.parsePacket(), 0U);                         // indistinguishable from "nothing" by length alone
  IS_EQUAL(CAN.packetId(), 0x0000003CU);                   // ...but the id says a frame arrived
  END_IT
}

bool test_on_receive_drains_every_pending_frame() {
  IT("the driver's own interrupt path drains both buffers in one go");
  IS_TRUE(startController());
  static uint8_t callbackCount;
  callbackCount = 0U;
  CAN.onReceive([](int) -> void { ++callbackCount; });
  const uint8_t payload[1] = { 0x55U };
  mcp2515.deliverExtendedFrame(0U, 0x0000004DU, payload, 1U);
  mcp2515.deliverExtendedFrame(1U, 0x0000005EU, payload, 1U);
  triggerInterrupt(kIntPin);                               // one edge, two pending frames
  IS_EQUAL(callbackCount, 2U);
  IS_EQUAL(mcp2515.reg(Mcp2515Model::regCanIntf) & 0x03U, 0x00U);
  CAN.onReceive(nullptr);
  END_IT
}

// ---- receive strategies a CAN handler can build on top of parsePacket() ----

// True when parsePacket() actually produced a frame. A zero-length frame returns 0 but leaves a
// valid id behind, which is how the driver's own interrupt path tells the two apart.
static bool takeOneFrame() {
  return (CAN.parsePacket() != 0U) || (CAN.packetId() != kNoId);
}

bool test_one_parse_per_pass_leaves_the_controller_backed_up() {
  IT("taking a single frame per pass leaves the second buffer full and the interrupt asserted");
  IS_TRUE(startController());
  const uint8_t payload[1] = { 0x77U };
  mcp2515.deliverExtendedFrame(0U, 0x0000006FU, payload, 1U);
  mcp2515.deliverExtendedFrame(1U, 0x00000070U, payload, 1U);
  IS_TRUE(takeOneFrame());
  // One frame consumed, one still pending: the MCP2515 holds its interrupt line low while any
  // receive flag stands, so an edge-triggered handler sees no further edge for the leftover.
  IS_EQUAL(mcp2515.reg(Mcp2515Model::regCanIntf) & 0x03U, 0x02U);
  END_IT
}

bool test_the_rx_pump_drains_the_controller_in_one_pass() {
  IT("CanRxPump::drain() empties both buffers in a single pass");
  IS_TRUE(startController());
  const uint8_t payload[1] = { 0x77U };
  mcp2515.deliverExtendedFrame(0U, 0x0000006FU, payload, 1U);
  mcp2515.deliverExtendedFrame(1U, 0x00000070U, payload, 1U);
  const CanRxPump::Result result = CanRxPump::drain(takeOneFrame, []() -> bool { return true; }, 8U);
  IS_EQUAL(result.handled, 2U);
  IS_FALSE(result.failed);
  IS_EQUAL(mcp2515.reg(Mcp2515Model::regCanIntf) & 0x03U, 0x00U);
  END_IT
}

bool test_the_rx_pump_dispatches_nothing_on_an_empty_controller() {
  IT("CanRxPump::drain() dispatches no frame when the controller is empty");
  IS_TRUE(startController());
  static uint8_t dispatched;
  dispatched = 0U;
  const CanRxPump::Result result = CanRxPump::drain(takeOneFrame, []() -> bool { ++dispatched; return true; }, 8U);
  IS_EQUAL(result.handled, 0U);
  IS_EQUAL(dispatched, 0U);                  // no frame is never mistaken for command 0x1FF
  END_IT
}

int main() {
  SUITE("MCP2515");
  test_begin_configures_the_controller();
  test_parse_packet_reports_nothing_when_no_frame_arrived();
  test_parse_packet_reads_an_extended_frame();
  test_parse_packet_drains_one_buffer_per_call();
  test_zero_length_frame_is_still_a_frame();
  test_on_receive_drains_every_pending_frame();
  test_one_parse_per_pass_leaves_the_controller_backed_up();
  test_the_rx_pump_drains_the_controller_in_one_pass();
  test_the_rx_pump_dispatches_nothing_on_an_empty_controller();
  FINISH
}
