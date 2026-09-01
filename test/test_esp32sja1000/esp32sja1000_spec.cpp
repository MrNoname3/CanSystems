#include "ESP32SJA1000.h"
#include "esp32CanModel.h"
#include "esp_intr_alloc.h"
#include "Arduino.h"
#include "BDDTest.h"

// The ESP32 CAN driver against the register-level model in test/_shims/src/esp32CanModel.h.
// These cover the gateway's transmit path - what endPacket() does when nothing on the bus
// answers, and what the driver does once the controller has dropped into bus-off - plus the
// receive contract the two drivers have to share.

static ESP32SJA1000& controller() {
  static ESP32SJA1000 can;
  return can;
}

static bool startController(Esp32CanModel::TxBehaviour behaviour) {
  esp32Can.reset();
  const bool started = controller().begin(500000U) == 1U;
  esp32Can.setTxBehaviour(behaviour);
  return started;
}

static bool sendOneFrame() {
  const uint8_t payload[2] = { 0xA1U, 0xB2U };
  if(controller().beginExtendedPacket(0x0000001AU, 2U) == 0U) { return false; }
  if(controller().write(payload, sizeof(payload)) != sizeof(payload)) { return false; }
  return controller().endPacket() != 0U;
}

// Hands the model one standard frame and takes it back out through the driver.
static bool receiveOneFrame(uint16_t id, uint8_t dlc) {
  const uint8_t payload[8] = { 0x5AU, 0xC3U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U };
  esp32Can.queueStandardFrame(id, payload, dlc);
  return controller().parsePacket() == dlc;
}

bool test_begin_leaves_the_controller_out_of_reset() {
  IT("begin() brings the controller out of reset mode");
  IS_TRUE(startController(Esp32CanModel::TxBehaviour::Completes));
  IS_FALSE(esp32Can.isInResetMode());
  END_IT
}

bool test_a_frame_is_transmitted_when_the_bus_answers() {
  IT("endPacket() reports success once the controller signals transmission complete");
  IS_TRUE(startController(Esp32CanModel::TxBehaviour::Completes));
  IS_TRUE(sendOneFrame());
  IS_EQUAL(esp32Can.getTransmitRequests(), 1U);
  END_IT
}

bool test_transmit_does_not_wait_for_the_bus() {
  IT("endPacket() hands the frame to the controller instead of waiting for the bus to take it");
  IS_TRUE(startController(Esp32CanModel::TxBehaviour::NeverEnds));
  setFakeMillis(0U);
  esp32Can.setPollDurationMs(1U);                 // one modelled millisecond per status poll
  IS_TRUE(controller().txReady());                // free before the first frame
  IS_TRUE(sendOneFrame());
  IS_EQUAL(esp32Can.getTransmitRequests(), 1U);
  // A pass of the cooperative loop cannot afford to wait out a 50 ms transmit timeout, so what
  // this asserts is that the clock barely moved.
  IS_TRUE(millis() < 10U);
  IS_FALSE(controller().txReady());               // the controller is still holding the frame
  clearFakeMillis();
  END_IT
}

bool test_a_frame_nobody_takes_is_abandoned() {
  IT("a transmit buffer still busy past the timeout is aborted, so later frames can go out");
  IS_TRUE(startController(Esp32CanModel::TxBehaviour::NeverEnds));
  setFakeMillis(0U);
  IS_TRUE(controller().txReady());
  IS_TRUE(sendOneFrame());
  IS_FALSE(controller().txReady());               // first refusal starts the clock on the frame
  setFakeMillis(1000U);
  IS_FALSE(controller().txReady());               // past the timeout the frame is given up on
  IS_EQUAL(esp32Can.getTransmitAborts(), 1U);

  esp32Can.setTxBehaviour(Esp32CanModel::TxBehaviour::Completes);
  IS_TRUE(controller().txReady());                // and the slot is usable again
  IS_TRUE(sendOneFrame());
  clearFakeMillis();
  END_IT
}

bool test_bus_off_is_recovered() {
  IT("a controller left in bus-off is brought back instead of staying mute");
  IS_TRUE(startController(Esp32CanModel::TxBehaviour::BusOff));
  IS_TRUE(controller().txReady());
  IS_TRUE(sendOneFrame());                        // the frame is lost, that is expected
  IS_TRUE(controller().isBusOff());
  IS_TRUE(esp32Can.isInResetMode());              // bus-off latched the controller in reset mode

  // Recovery is the driver's job on this part: the CAN spec's 128x11 recessive-bit wait only
  // starts once software clears the reset bit, so nothing happens until someone does.
  controller().recoverFromBusOff();
  IS_FALSE(esp32Can.isInResetMode());

  esp32Can.setTxBehaviour(Esp32CanModel::TxBehaviour::Completes);
  IS_TRUE(controller().txReady());
  IS_TRUE(sendOneFrame());                        // the bus works again
  END_IT
}

bool test_a_standard_frame_is_decoded() {
  IT("parsePacket() decodes a standard frame and hands its payload over");
  IS_TRUE(startController(Esp32CanModel::TxBehaviour::Completes));
  IS_TRUE(receiveOneFrame(0x123U, 2U));
  IS_EQUAL(controller().packetId(), 0x123U);
  IS_FALSE(controller().packetExtended());
  IS_FALSE(controller().packetRtr());
  uint8_t received[2] = { 0U };
  IS_EQUAL(controller().readBytes(received, sizeof(received)), sizeof(received));
  IS_EQUAL(received[0], 0x5AU);
  IS_EQUAL(received[1], 0xC3U);
  END_IT
}

bool test_an_empty_controller_leaves_no_frame_behind() {
  IT("parsePacket() clears the previous frame once the controller has nothing waiting");
  IS_TRUE(startController(Esp32CanModel::TxBehaviour::Completes));
  IS_TRUE(receiveOneFrame(0x123U, 2U));
  uint8_t received[2] = { 0U };
  IS_EQUAL(controller().readBytes(received, sizeof(received)), sizeof(received));

  // Reading the frame released the buffer, so this pass finds an empty controller. The AVR
  // side tells a zero-length frame from "nothing" by the id alone, which only works while both
  // drivers answer noId here.
  IS_EQUAL(controller().parsePacket(), 0U);
  IS_EQUAL(controller().packetId(), CANController::noId);
  IS_EQUAL(controller().packetDlc(), 0U);
  IS_EQUAL(controller().available(), 0);
  END_IT
}

int main() {
  SUITE("ESP32SJA1000");
  test_begin_leaves_the_controller_out_of_reset();
  test_a_frame_is_transmitted_when_the_bus_answers();
  test_transmit_does_not_wait_for_the_bus();
  test_a_frame_nobody_takes_is_abandoned();
  test_bus_off_is_recovered();
  test_a_standard_frame_is_decoded();
  test_an_empty_controller_leaves_no_frame_behind();
  FINISH
}
