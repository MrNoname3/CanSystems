#include "canHandlerEsp32.hpp"
#include "esp32CanModel.h"
#include "esp_intr_alloc.h"
#include "EEPROM.h"
#include "crc16.hpp"
#include "Arduino.h"
#include "BDDTest.h"

// The ESP32 CAN handler against the register-level controller model in
// test/_shims/src/esp32CanModel.h and the FreeRTOS shims: the frames a test queues are the ones
// the driver really parses, and the frames it asserts on are the ones that really reached the
// controller's transmit window.

static constexpr uint16_t kMasterId = 10U;
static constexpr uint16_t kLocalId = 11U;
static constexpr uint16_t kDeviceId = 26U;

// The layout EEPROMHandler<CanId, 0> stores, so init() finds ids it accepts. The checksum covers
// the whole record with its own field still zero, which is how the handler writes and reads it.
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
  esp32Can.reset();
  setFakeMillis(0U);
  seedCanIds(kMasterId, kLocalId);
}

/// The id bookkeeping on its own, with the protected helpers exposed. CanHandlerEsp32 is final,
/// and the ids live one level up anyway - this is the layer SET_CAN_ID relies on.
class TestIdHolder final : public CanHandlerBase {
public:
  bool init() override { return true; }
  bool run() override { return true; }
  bool send(uint16_t command, const uint8_t (&data)[8]) const override { // NOLINT(modernize-use-nodiscard)
    (void)command;
    (void)data;
    return true;
  }
  using CanHandlerBase::loadCanIds;
  using CanHandlerBase::saveCanIds;
  using CanHandlerBase::storeCanIds;
};

/// A registered CAN device that records what it was handed.
class TestDevice final : public CanBase {
public:
  TestDevice(CanHandler& canHandler, uint16_t clientCanId) :
    CanBase(canHandler, clientCanId) {}

  bool init() override { return true; }  // NOLINT(readability-make-member-function-const)
  bool run() override { return true; }   // NOLINT(readability-make-member-function-const)

  void canFrameArrivedCallback(const CanHandler::CanFrame& canFrame) override {
    ++received;
    lastCommand = static_cast<uint16_t>(canFrame.cmd);
    lastFrom = static_cast<uint16_t>(canFrame.from);
    lastData0 = canFrame.data[0];
  }

  uint32_t received = 0U;
  uint16_t lastCommand = 0U;
  uint16_t lastFrom = 0U;
  uint8_t lastData0 = 0U;
};

/// A device that asks the handler about another address from inside its frame callback, which is
/// where a driver handling a renumbering answer would ask.
class TestAskingDevice final : public CanBase {
public:
  TestAskingDevice(CanHandler& canHandler, uint16_t clientCanId, uint16_t askedAbout) :
    CanBase(canHandler, clientCanId),
    askedAbout(askedAbout) {}

  bool init() override { return true; }  // NOLINT(readability-make-member-function-const)
  bool run() override { return true; }   // NOLINT(readability-make-member-function-const)

  void canFrameArrivedCallback(const CanHandler::CanFrame& canFrame) override {
    (void)canFrame;
    answeredFree = isClientCanIdFree(askedAbout);
    ++received;
  }

  uint16_t askedAbout;
  bool answeredFree = false;
  uint32_t received = 0U;
};

/// Builds the extended identifier the handler packs a frame into.
static uint32_t extIdOf(uint16_t to, uint16_t cmd, uint16_t from) {
  const uint8_t empty[8] = { 0U };
  return CanHandler::CanFrame(to, cmd, from, empty).extId;
}

/// Delivers one frame to the handler the way the peripheral does: the controller holds it, the
/// interrupt fires, and the handler's own run() drains the receive queue.
static void deliverFrame(CanHandler& handler, uint16_t from, uint16_t cmd, uint8_t payload0) {
  const uint8_t data[8] = { payload0, 0U, 0U, 0U, 0U, 0U, 0U, 0U };
  esp32Can.queueExtendedFrame(extIdOf(kLocalId, cmd, from), data, 8U);
  esp32TriggerCanInterrupt();
  (void)handler.run();
}

// ---- initialisation ----

bool test_init_brings_the_controller_up() {
  IT("init() loads the stored ids and configures the controller");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  IS_TRUE(handler.init());
  IS_EQUAL(handler.getMasterCanId(), kMasterId);
  IS_EQUAL(handler.getLocalCanId(), kLocalId);
  END_IT
}

bool test_init_fails_without_stored_ids() {
  IT("init() fails when the EEPROM holds no valid ids");
  resetEnv();
  EEPROM.clear();                                   // no record, so the checksum cannot match
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  IS_FALSE(handler.init());
  END_IT
}

// ---- receive path: interrupt -> queue -> device ----

bool test_a_frame_reaches_the_device_registered_for_its_sender() {
  IT("a received frame is handed to the device registered for its sender id");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  TestDevice device(handler, kDeviceId);
  IS_TRUE(handler.init());

  deliverFrame(handler, kDeviceId, static_cast<uint16_t>(CanCmd::BUTTON_EVENT), 0x42U);

  IS_EQUAL(device.received, 1U);
  IS_EQUAL(device.lastCommand, static_cast<uint16_t>(CanCmd::BUTTON_EVENT));
  IS_EQUAL(device.lastFrom, kDeviceId);
  IS_EQUAL(device.lastData0, 0x42U);
  END_IT
}

bool test_a_frame_from_an_unknown_sender_reaches_nobody() {
  IT("a frame whose sender has no registered device is dropped without disturbing the others");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  TestDevice device(handler, kDeviceId);
  IS_TRUE(handler.init());

  deliverFrame(handler, 27U, static_cast<uint16_t>(CanCmd::BUTTON_EVENT), 0x42U);

  IS_EQUAL(device.received, 0U);
  END_IT
}

bool test_each_device_only_sees_its_own_sender() {
  IT("two registered devices each receive only the frames from their own sender id");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  TestDevice first(handler, kDeviceId);
  TestDevice second(handler, 27U);
  IS_TRUE(handler.init());

  deliverFrame(handler, kDeviceId, static_cast<uint16_t>(CanCmd::PING), 1U);
  deliverFrame(handler, 27U, static_cast<uint16_t>(CanCmd::PING), 2U);
  deliverFrame(handler, 27U, static_cast<uint16_t>(CanCmd::PING), 3U);

  IS_EQUAL(first.received, 1U);
  IS_EQUAL(second.received, 2U);
  IS_EQUAL(second.lastData0, 3U);
  END_IT
}

bool test_a_device_on_the_handlers_own_id_is_not_registered() {
  IT("a device claiming the handler's own or the master's id is not registered");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  IS_TRUE(handler.init());
  // Built after init(): the check reads the handler's ids, and those are only loaded there.
  TestDevice sameAsLocal(handler, kLocalId);
  TestDevice sameAsMaster(handler, kMasterId);

  deliverFrame(handler, kLocalId, static_cast<uint16_t>(CanCmd::PING), 1U);
  deliverFrame(handler, kMasterId, static_cast<uint16_t>(CanCmd::PING), 1U);

  IS_EQUAL(sameAsLocal.received, 0U);
  IS_EQUAL(sameAsMaster.received, 0U);
  END_IT
}

bool test_a_device_built_before_init_on_a_reserved_id_stops_init() {
  IT("init() refuses to come up when a registered device holds the handler's own id");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  // Built before init(), the way the gateway's drivers are: at this point the handler's own ids
  // are still unread, so nothing the constructor could compare against exists yet.
  TestDevice clash(handler, kLocalId);
  IS_EQUAL(clash.getClientCanId(), kLocalId);       // in the list, registered from its constructor
  IS_FALSE(handler.init());
  END_IT
}

bool test_a_device_built_before_init_on_the_master_id_stops_init() {
  IT("init() refuses to come up when a registered device holds the master's id");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  TestDevice clash(handler, kMasterId);
  IS_EQUAL(clash.getClientCanId(), kMasterId);
  IS_FALSE(handler.init());
  END_IT
}

bool test_devices_on_free_ids_let_init_through() {
  IT("init() comes up with devices that hold ids of their own");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  TestDevice first(handler, kDeviceId);
  TestDevice second(handler, 27U);
  IS_TRUE(handler.init());
  deliverFrame(handler, kDeviceId, static_cast<uint16_t>(CanCmd::PING), 1U);
  IS_EQUAL(first.received, 1U);
  IS_EQUAL(second.received, 0U);
  END_IT
}

bool test_registering_nothing_is_refused() {
  IT("registerCallback refuses a null device");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  IS_FALSE(handler.registerCallback(nullptr));
  END_IT
}

// ---- transmit path: queue -> controller ----

bool test_a_sent_frame_reaches_the_bus() {
  IT("a frame handed to send() reaches the controller's transmit window on the next pass");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  IS_TRUE(handler.init());

  const uint8_t payload[8] = { 9U, 8U, 7U, 6U, 5U, 4U, 3U, 2U };
  IS_TRUE(handler.send(CanHandler::CanFrame(kDeviceId, static_cast<uint16_t>(CanCmd::PING), kLocalId, payload)));
  IS_EQUAL(esp32Can.transmitted().size(), 0U);      // still only queued
  IS_TRUE(handler.run());

  IS_EQUAL(esp32Can.transmitted().size(), 1U);
  const Esp32CanModel::SentFrame& sent = esp32Can.transmitted().front();
  IS_TRUE(sent.extended);
  IS_EQUAL(sent.id, extIdOf(kDeviceId, static_cast<uint16_t>(CanCmd::PING), kLocalId));
  IS_EQUAL(sent.dlc, 8U);
  IS_EQUAL(sent.data[0], 9U);
  IS_EQUAL(sent.data[7], 2U);
  END_IT
}

bool test_a_busy_controller_keeps_the_frame_queued() {
  IT("a frame is not taken off the queue while the controller has no room for it");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  IS_TRUE(handler.init());

  const uint8_t payload[8] = { 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U };
  IS_TRUE(handler.send(CanHandler::CanFrame(kDeviceId, static_cast<uint16_t>(CanCmd::PING), kLocalId, payload)));

  esp32Can.setReg(Esp32CanModel::regSr, 0U);        // transmit buffer occupied
  IS_TRUE(handler.run());
  IS_EQUAL(esp32Can.transmitted().size(), 0U);      // nothing went out, and nothing was lost

  esp32Can.setReg(Esp32CanModel::regSr, Esp32CanModel::srTxBufferFree | Esp32CanModel::srTxComplete);
  IS_TRUE(handler.run());
  IS_EQUAL(esp32Can.transmitted().size(), 1U);
  END_IT
}

bool test_the_transmit_queue_refuses_what_it_cannot_hold() {
  IT("send() reports the frame it could not queue rather than dropping it silently");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  IS_TRUE(handler.init());

  esp32Can.setReg(Esp32CanModel::regSr, 0U);        // nothing drains while the queue fills
  const uint8_t payload[8] = { 0U };
  uint32_t queued = 0U;
  for(uint32_t i = 0U; i < 200U; i++) {
    if(handler.send(CanHandler::CanFrame(kDeviceId, static_cast<uint16_t>(CanCmd::PING), kLocalId, payload))) {
      ++queued;
    }
  }
  IS_EQUAL(queued, 100U);                           // the queue's depth, and not one frame more
  END_IT
}

bool test_the_master_does_not_send_to_itself() {
  IT("send() by command refuses to address the master when this node is the master");
  resetEnv();
  seedCanIds(kMasterId, kMasterId);                 // master and local are the same node
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  IS_TRUE(handler.init());

  const uint8_t payload[8] = { 0U };
  IS_FALSE(handler.send(static_cast<uint16_t>(CanCmd::PING), payload));
  END_IT
}

// ---- bus-off ----

bool test_bus_off_is_recovered() {
  IT("a controller that has dropped into bus-off is brought back by the next pass");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  IS_TRUE(handler.init());

  esp32Can.setTxBehaviour(Esp32CanModel::TxBehaviour::BusOff);
  const uint8_t payload[8] = { 0U };
  IS_TRUE(handler.send(CanHandler::CanFrame(kDeviceId, static_cast<uint16_t>(CanCmd::PING), kLocalId, payload)));
  (void)handler.run();                              // the send drives the controller into bus-off
  IS_TRUE(esp32Can.isInResetMode());

  esp32Can.setTxBehaviour(Esp32CanModel::TxBehaviour::Completes);
  (void)handler.run();                              // the next pass notices and recovers
  IS_FALSE(esp32Can.isInResetMode());
  END_IT
}

bool test_storing_ids_leaves_the_running_ones_alone() {
  IT("storeCanIds writes the new ids without moving the ones the handler is running with");
  resetEnv();
  TestIdHolder handler;
  IS_TRUE(handler.loadCanIds());
  IS_EQUAL(handler.getLocalCanId(), kLocalId);

  // Every outgoing frame is stamped with the running id, so an answer sent after a change has
  // to still carry the old address - the master is not listening for the new one yet.
  IS_TRUE(handler.storeCanIds(kMasterId, 28U));
  IS_EQUAL(handler.getLocalCanId(), kLocalId);
  IS_EQUAL(handler.getMasterCanId(), kMasterId);

  // The next start is what picks the new address up.
  IS_TRUE(handler.loadCanIds());
  IS_EQUAL(handler.getLocalCanId(), 28U);
  END_IT
}

bool test_saving_ids_moves_the_running_ones() {
  IT("saveCanIds does move them, which is why the assignment path does not use it");
  resetEnv();
  TestIdHolder handler;
  IS_TRUE(handler.loadCanIds());
  IS_TRUE(handler.saveCanIds(kMasterId, 28U));
  IS_EQUAL(handler.getLocalCanId(), 28U);
  END_IT
}

bool test_a_device_can_ask_about_an_address_from_its_own_callback() {
  IT("a device asking whether an address is free from its frame callback gets the real answer");
  resetEnv();
  ESP32SJA1000 controller;
  CanHandler handler(controller);
  static constexpr uint16_t kFreeId = 30U;
  TestAskingDevice device(handler, kDeviceId, kFreeId);
  Task& task = handler;
  IS_TRUE(task.init());

  // The callback runs while the handler holds the device list, so the question arrives from
  // inside the lock the answer needs.
  deliverFrame(handler, kDeviceId, static_cast<uint16_t>(CanCmd::PING), 0U);
  IS_EQUAL(device.received, 1U);
  IS_TRUE(device.answeredFree);            // nothing is registered on kFreeId
  END_IT
}

int main() {
  SUITE("CanHandlerEsp32");
  test_init_brings_the_controller_up();
  test_init_fails_without_stored_ids();
  test_a_frame_reaches_the_device_registered_for_its_sender();
  test_a_frame_from_an_unknown_sender_reaches_nobody();
  test_each_device_only_sees_its_own_sender();
  test_a_device_on_the_handlers_own_id_is_not_registered();
  test_a_device_built_before_init_on_a_reserved_id_stops_init();
  test_a_device_built_before_init_on_the_master_id_stops_init();
  test_devices_on_free_ids_let_init_through();
  test_registering_nothing_is_refused();
  test_a_sent_frame_reaches_the_bus();
  test_a_busy_controller_keeps_the_frame_queued();
  test_the_transmit_queue_refuses_what_it_cannot_hold();
  test_the_master_does_not_send_to_itself();
  test_bus_off_is_recovered();
  test_storing_ids_leaves_the_running_ones_alone();
  test_saving_ids_moves_the_running_ones();
  test_a_device_can_ask_about_an_address_from_its_own_callback();
  FINISH
}
