#include "ESP32SJA1000.h"
#include "esp32CanModel.h"
#include "esp_intr_alloc.h"
#include "Arduino.h"
#include "BDDTest.h"

// The ESP32 CAN driver against the register-level model in test/_shims/src/esp32CanModel.h.
// The gateway's transmit path is what these cover: what endPacket() does when nothing on the
// bus answers, and what the driver does once the controller has dropped into bus-off.

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

bool test_transmit_gives_up_when_nothing_answers() {
  IT("endPacket() gives up instead of polling forever when transmission never completes");
  IS_TRUE(startController(Esp32CanModel::TxBehaviour::NeverEnds));
  setFakeMillis(0U);
  esp32Can.setPollDurationMs(1U);                 // one modelled millisecond per status poll
  IS_FALSE(sendOneFrame());                       // reports failure rather than hanging
  // A CAN frame at 500 kbit/s is a few hundred microseconds; the wait must end far short of
  // the gateway's 10 s task-watchdog budget.
  IS_TRUE(esp32Can.getStatusReads() < 200U);
  clearFakeMillis();
  END_IT
}

bool test_bus_off_is_recovered() {
  IT("a controller left in bus-off is brought back instead of staying mute");
  IS_TRUE(startController(Esp32CanModel::TxBehaviour::BusOff));
  IS_FALSE(sendOneFrame());                       // the frame is lost, that is expected
  IS_TRUE((esp32Can.reg(Esp32CanModel::regSr) & Esp32CanModel::srBusOff) != 0U);
  IS_TRUE(esp32Can.isInResetMode());              // bus-off latched the controller in reset mode

  // Recovery is the driver's job on this part: the CAN spec's 128x11 recessive-bit wait only
  // starts once software clears the reset bit, so nothing happens until someone does.
  controller().recoverFromBusOff();
  IS_FALSE(esp32Can.isInResetMode());

  esp32Can.setTxBehaviour(Esp32CanModel::TxBehaviour::Completes);
  IS_TRUE(sendOneFrame());                        // the bus works again
  END_IT
}

int main() {
  SUITE("ESP32SJA1000");
  test_begin_leaves_the_controller_out_of_reset();
  test_a_frame_is_transmitted_when_the_bus_answers();
  test_transmit_gives_up_when_nothing_answers();
  test_bus_off_is_recovered();
  FINISH
}
