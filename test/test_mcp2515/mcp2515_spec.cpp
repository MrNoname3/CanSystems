#include "MCP2515.h"
#include "canFramePump.hpp"
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

// Queues one 1-byte extended frame. The identifier's low byte lands in the transmit buffer's
// EID0 register, which is what the ordering assertions read back.
static bool queueFrame(uint8_t marker) {
  const uint8_t payload[1] = { 0x11U };
  if(CAN.beginExtendedPacket(0x1FF00000U | marker, 1U) == 0U) { return false; }
  if(CAN.write(payload, 1U) != 1U) { return false; }
  return CAN.endPacket() != 0U;
}

// TXBnCTRL / TXBnEID0 register addresses, spelled out so a failing line names its buffer.
static constexpr uint8_t kTxb0Ctrl = 0x30U;
static constexpr uint8_t kTxb1Ctrl = 0x40U;
static constexpr uint8_t kTxb2Ctrl = 0x50U;
static constexpr uint8_t kTxb0Eid0 = 0x34U;
static constexpr uint8_t kTxb1Eid0 = 0x44U;
static constexpr uint8_t kTxb2Eid0 = 0x54U;

bool test_writing_past_the_payload_reports_what_fit() {
  IT("writing more than a CAN frame holds reports how much was taken, not success");
  IS_TRUE(startController());
  const uint8_t tooMuch[9] = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U };
  IS_EQUAL(CAN.beginExtendedPacket(0x1FF00001U, 8U), 1U);
  // The payload is eight bytes; the ninth is dropped. A caller that only checks for a non-zero
  // return would treat this as a full write.
  IS_EQUAL(CAN.write(tooMuch, sizeof(tooMuch)), 8U);
  END_IT
}

bool test_the_first_receive_buffer_rolls_over_into_the_second() {
  IT("receive buffer 0 is set to roll a frame over into buffer 1 when it is full");
  IS_TRUE(startController());
  // Without the rollover bit the second of two frames arriving back to back is lost even though
  // buffer 1 is empty. The bit exists on buffer 0's control register only.
  IS_EQUAL(mcp2515.reg(Mcp2515Model::rxCtrl(0U)) & 0x04U, 0x04U);
  IS_EQUAL(mcp2515.reg(Mcp2515Model::rxCtrl(1U)) & 0x04U, 0x00U);
  END_IT
}

bool test_setting_an_extended_filter_keeps_the_rollover() {
  IT("installing an extended filter rewrites the control register but keeps the rollover");
  IS_TRUE(startController());
  IS_EQUAL(CAN.filterExtended(0x12345678U, 0x1FFFFFFFU), 1U);
  IS_EQUAL(mcp2515.reg(Mcp2515Model::rxCtrl(0U)) & 0x04U, 0x04U);
  IS_EQUAL(mcp2515.reg(Mcp2515Model::rxCtrl(1U)) & 0x04U, 0x00U);
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

// ---- transmit path ----

bool test_queued_frames_keep_their_sending_order() {
  IT("frames queued back to back are held oldest-first across the three transmit buffers");
  IS_TRUE(startController());
  IS_TRUE(queueFrame(0x01U));
  IS_TRUE(queueFrame(0x02U));
  IS_TRUE(queueFrame(0x03U));
  // At equal transmit priority the MCP2515 sends the highest-numbered buffer first, so the
  // oldest frame has to sit in TXB2 and the newest in TXB0 for insertion order to survive.
  IS_EQUAL(mcp2515.reg(kTxb2Eid0), 0x01U);
  IS_EQUAL(mcp2515.reg(kTxb1Eid0), 0x02U);
  IS_EQUAL(mcp2515.reg(kTxb0Eid0), 0x03U);
  // Equal priority is what makes the buffer number the tie-breaker.
  IS_EQUAL(mcp2515.reg(kTxb2Ctrl) & 0x03U, 0x00U);
  IS_EQUAL(mcp2515.reg(kTxb1Ctrl) & 0x03U, 0x00U);
  IS_EQUAL(mcp2515.reg(kTxb0Ctrl) & 0x03U, 0x00U);
  END_IT
}

bool test_transmit_gives_up_when_the_bus_never_takes_a_frame() {
  IT("endPacket() gives up instead of polling forever once no buffer frees up");
  IS_TRUE(startController());
  mcp2515.setTxBehaviour(Mcp2515Model::TxBehaviour::NeverEnds);
  setFakeMillis(0U);
  mcp2515.setPollDurationMs(1U);                 // one modelled millisecond per buffer poll
  IS_TRUE(queueFrame(0x01U));
  IS_TRUE(queueFrame(0x02U));
  IS_TRUE(queueFrame(0x03U));
  IS_FALSE(queueFrame(0x04U));                   // every buffer is full and none empties
  // The stuck frames are dropped rather than left blocking every later send.
  IS_EQUAL(mcp2515.reg(kTxb2Ctrl) & Mcp2515Model::flagTxReq, 0x00U);
  IS_EQUAL(mcp2515.reg(kTxb1Ctrl) & Mcp2515Model::flagTxReq, 0x00U);
  IS_EQUAL(mcp2515.reg(kTxb0Ctrl) & Mcp2515Model::flagTxReq, 0x00U);
  clearFakeMillis();
  END_IT
}

bool test_the_buffer_cycle_restarts_once_the_frames_are_gone() {
  IT("the fourth frame goes back to the top of the cycle once the first three have left");
  IS_TRUE(startController());                    // frames complete by default
  IS_TRUE(queueFrame(0x01U));
  IS_TRUE(queueFrame(0x02U));
  IS_TRUE(queueFrame(0x03U));
  IS_TRUE(queueFrame(0x04U));
  IS_EQUAL(mcp2515.reg(kTxb2Eid0), 0x04U);
  END_IT
}

bool test_flush_confirms_the_queued_frames_are_gone() {
  IT("flushTx() reports success once the controller has emptied its transmit buffers");
  IS_TRUE(startController());                    // frames complete by default
  IS_TRUE(queueFrame(0x01U));
  IS_TRUE(CAN.flushTx());
  END_IT
}

bool test_flush_reports_a_bus_that_takes_nothing() {
  IT("flushTx() reports failure and drops the stuck frame when the bus takes nothing");
  IS_TRUE(startController());
  mcp2515.setTxBehaviour(Mcp2515Model::TxBehaviour::NeverEnds);
  setFakeMillis(0U);
  mcp2515.setPollDurationMs(1U);                 // one modelled millisecond per buffer poll
  IS_TRUE(queueFrame(0x01U));
  IS_FALSE(CAN.flushTx());
  IS_EQUAL(mcp2515.reg(kTxb2Ctrl) & Mcp2515Model::flagTxReq, 0x00U);
  // Tens of milliseconds, not the unbounded spin it replaced: this is what an AVR main loop
  // pays once for a dead bus, and it has to stay far below the node's loop-time budget.
  IS_TRUE(millis() < 30U);
  clearFakeMillis();
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
  IT("CanFramePump::drain() empties both buffers in a single pass");
  IS_TRUE(startController());
  const uint8_t payload[1] = { 0x77U };
  mcp2515.deliverExtendedFrame(0U, 0x0000006FU, payload, 1U);
  mcp2515.deliverExtendedFrame(1U, 0x00000070U, payload, 1U);
  const CanFramePump::Result result = CanFramePump::drain(takeOneFrame, []() -> bool { return true; }, 8U);
  IS_EQUAL(result.handled, 2U);
  IS_FALSE(result.failed);
  IS_EQUAL(mcp2515.reg(Mcp2515Model::regCanIntf) & 0x03U, 0x00U);
  END_IT
}

bool test_the_rx_pump_dispatches_nothing_on_an_empty_controller() {
  IT("CanFramePump::drain() dispatches no frame when the controller is empty");
  IS_TRUE(startController());
  static uint8_t dispatched;
  dispatched = 0U;
  const CanFramePump::Result result = CanFramePump::drain(takeOneFrame, []() -> bool { ++dispatched; return true; }, 8U);
  IS_EQUAL(result.handled, 0U);
  IS_EQUAL(dispatched, 0U);                  // no frame is never mistaken for command 0x1FF
  END_IT
}

int main() {
  SUITE("MCP2515");
  test_begin_configures_the_controller();
  test_writing_past_the_payload_reports_what_fit();
  test_the_first_receive_buffer_rolls_over_into_the_second();
  test_setting_an_extended_filter_keeps_the_rollover();
  test_parse_packet_reports_nothing_when_no_frame_arrived();
  test_parse_packet_reads_an_extended_frame();
  test_parse_packet_drains_one_buffer_per_call();
  test_zero_length_frame_is_still_a_frame();
  test_on_receive_drains_every_pending_frame();
  test_queued_frames_keep_their_sending_order();
  test_transmit_gives_up_when_the_bus_never_takes_a_frame();
  test_the_buffer_cycle_restarts_once_the_frames_are_gone();
  test_flush_confirms_the_queued_frames_are_gone();
  test_flush_reports_a_bus_that_takes_nothing();
  test_one_parse_per_pass_leaves_the_controller_backed_up();
  test_the_rx_pump_drains_the_controller_in_one_pass();
  test_the_rx_pump_dispatches_nothing_on_an_empty_controller();
  FINISH
}
