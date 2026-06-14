#include "canHandlerBase.hpp"
#include "EEPROM.h"
#include "BDDTest.h"
#include <string.h>

// Minimal concrete subclass that records the last send() call.
class TestCanHandler final : public CanHandlerBase {
public:
  using CanHandlerBase::loadCanIds;
  using CanHandlerBase::saveCanIds;
  using CanHandlerBase::send;

  mutable uint16_t lastCommand = 0U;
  mutable uint8_t lastData[8] = {};

  bool init() override { return true; } // NOLINT(readability-make-member-function-const)
  bool run() override { return true; } // NOLINT(readability-make-member-function-const)

  [[nodiscard]] bool send(uint16_t command, const uint8_t (&data)[8]) const override {
    lastCommand = command;
    memcpy(lastData, data, 8U);
    return true;
  }
};

// ---- CanFrame construction ----

bool test_canframe_default_constructor() {
  IT("CanFrame default constructor zeroes extId and all data bytes");
  CanHandlerBase::CanFrame f;
  IS_EQUAL(f.extId, 0U);
  IS_EQUAL(f.to, 0U);
  IS_EQUAL(f.cmd, 0U);
  IS_EQUAL(f.from, 0U);
  IS_EQUAL(f.data[0], 0U);
  IS_EQUAL(f.data[7], 0U);
  END_IT
}

bool test_canframe_field_packing() {
  IT("CanFrame(to, cmd, from, data) packs to/cmd/from into extId correctly");
  const uint8_t data[8] = {};
  // extId = 1 | (1<<10) | (1<<19) = 0x00080401
  CanHandlerBase::CanFrame f(1U, 1U, 1U, data);
  IS_EQUAL(f.to, 1U);
  IS_EQUAL(f.cmd, 1U);
  IS_EQUAL(f.from, 1U);
  IS_EQUAL(f.extId, 0x00080401U);
  END_IT
}

bool test_canframe_field_unpacking_via_extid() {
  IT("setting extId directly gives correct to/cmd/from field values");
  CanHandlerBase::CanFrame f;
  // to=0x3FF (bits 0-9), cmd=0x1FF (bits 10-18), from=0x3FF (bits 19-28)
  // extId = 0x3FF | (0x1FF<<10) | (0x3FF<<19) = 0x1FFFFFFF
  f.extId = 0x1FFFFFFFU;
  IS_EQUAL(f.to, 0x3FFU);
  IS_EQUAL(f.cmd, 0x1FFU);
  IS_EQUAL(f.from, 0x3FFU);
  END_IT
}

bool test_canframe_to_overflow_masked() {
  IT("to value with bits beyond bit 9 is masked to 10 bits on packing");
  const uint8_t data[8] = {};
  CanHandlerBase::CanFrame f(0x7FFU, 0U, 0U, data); // 0x7FF & 0x3FF = 0x3FF
  IS_EQUAL(f.to, 0x3FFU);
  IS_EQUAL(f.cmd, 0U);
  IS_EQUAL(f.from, 0U);
  END_IT
}

bool test_canframe_cmd_overflow_masked() {
  IT("cmd value with bits beyond bit 8 is masked to 9 bits on packing");
  const uint8_t data[8] = {};
  CanHandlerBase::CanFrame f(0U, 0x3FFU, 0U, data); // 0x3FF & 0x1FF = 0x1FF
  IS_EQUAL(f.to, 0U);
  IS_EQUAL(f.cmd, 0x1FFU);
  IS_EQUAL(f.from, 0U);
  END_IT
}

bool test_canframe_max_field_values() {
  IT("CanFrame with all max field values produces extId = 0x1FFFFFFF");
  const uint8_t data[8] = {};
  CanHandlerBase::CanFrame f(0x3FFU, 0x1FFU, 0x3FFU, data);
  IS_EQUAL(f.extId, 0x1FFFFFFFU);
  IS_EQUAL(f.to, 0x3FFU);
  IS_EQUAL(f.cmd, 0x1FFU);
  IS_EQUAL(f.from, 0x3FFU);
  END_IT
}

bool test_canframe_data_copied() {
  IT("CanFrame constructor copies the 8-byte payload verbatim");
  const uint8_t data[8] = { 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U };
  CanHandlerBase::CanFrame f(0U, 0U, 0U, data);
  IS_EQUAL(f.data[0], 0x01U);
  IS_EQUAL(f.data[7], 0x08U);
  END_IT
}

// ---- filter mask and ID helpers ----

bool test_filter_mask_value() {
  IT("getCanIdFilterMask returns 0x3FF (lower 10 bits)");
  IS_EQUAL(CanHandlerBase::getCanIdFilterMask(), 0x3FFU);
  END_IT
}

// ---- EEPROM-backed ID persistence ----

bool test_load_fails_on_fresh_eeprom() {
  IT("loadCanIds returns false when EEPROM has not been written (CRC mismatch)");
  EEPROM.clear();
  TestCanHandler h;
  IS_FALSE(h.loadCanIds());
  END_IT
}

bool test_save_load_roundtrip() {
  IT("saveCanIds then loadCanIds restores master and local IDs exactly");
  EEPROM.clear();
  TestCanHandler h;
  IS_TRUE(h.saveCanIds(100U, 200U));
  IS_TRUE(h.loadCanIds());
  IS_EQUAL(h.getMasterCanId(), 100U);
  IS_EQUAL(h.getLocalCanId(), 200U);
  END_IT
}

bool test_save_masks_ids_to_10_bits() {
  IT("saveCanIds masks IDs to 10 bits before storing");
  EEPROM.clear();
  TestCanHandler h;
  IS_TRUE(h.saveCanIds(0x7FFU, 0xBFFU)); // 0x7FF & 0x3FF = 0x3FF, 0xBFF & 0x3FF = 0x3FF
  IS_TRUE(h.loadCanIds());
  IS_EQUAL(h.getMasterCanId(), 0x3FFU);
  IS_EQUAL(h.getLocalCanId(), 0x3FFU);
  END_IT
}

bool test_is_device_master_true() {
  IT("isDeviceMaster returns true when master and local IDs are equal");
  EEPROM.clear();
  TestCanHandler h;
  IS_TRUE(h.saveCanIds(100U, 100U));
  IS_TRUE(h.loadCanIds());
  IS_TRUE(h.isDeviceMaster());
  END_IT
}

bool test_is_device_master_false() {
  IT("isDeviceMaster returns false when master and local IDs differ");
  EEPROM.clear();
  TestCanHandler h;
  IS_TRUE(h.saveCanIds(100U, 200U));
  IS_TRUE(h.loadCanIds());
  IS_FALSE(h.isDeviceMaster());
  END_IT
}

bool test_get_filtered_id() {
  IT("getCanFilteredId returns localId already masked to 10 bits");
  EEPROM.clear();
  TestCanHandler h;
  IS_TRUE(h.saveCanIds(0U, 0x3ABU));
  IS_TRUE(h.loadCanIds());
  IS_EQUAL(h.getCanFilteredId(), 0x3ABU);
  IS_EQUAL(h.getCanFilteredId(), h.getLocalCanId() & CanHandlerBase::getCanIdFilterMask());
  END_IT
}

// ---- send() overload dispatch ----

bool test_send_cmd_enum_dispatches() {
  IT("send(CanCmd) converts enum to uint16_t and sends zero-filled data");
  TestCanHandler h;
  IS_TRUE(h.send(CanCmd::PING));
  IS_EQUAL(h.lastCommand, static_cast<uint16_t>(CanCmd::PING));
  IS_EQUAL(h.lastData[0], 0U);
  IS_EQUAL(h.lastData[7], 0U);
  END_IT
}

bool test_send_response_ack() {
  IT("send(CanCmd, Response::ACK) puts 0x01 in data[0] and zeros in the rest");
  TestCanHandler h;
  IS_TRUE(h.send(CanCmd::OTA_START, CanHandlerBase::Response::ACK));
  IS_EQUAL(h.lastCommand, static_cast<uint16_t>(CanCmd::OTA_START));
  IS_EQUAL(h.lastData[0], 0x01U);
  IS_EQUAL(h.lastData[1], 0U);
  IS_EQUAL(h.lastData[7], 0U);
  END_IT
}

bool test_send_response_nack() {
  IT("send(CanCmd, Response::NACK) puts 0x00 in data[0]");
  TestCanHandler h;
  IS_TRUE(h.send(CanCmd::OTA_START, CanHandlerBase::Response::NACK));
  IS_EQUAL(h.lastData[0], 0x00U);
  END_IT
}

int main() {
  SUITE("CanHandlerBase");
  test_canframe_default_constructor();
  test_canframe_field_packing();
  test_canframe_field_unpacking_via_extid();
  test_canframe_to_overflow_masked();
  test_canframe_cmd_overflow_masked();
  test_canframe_max_field_values();
  test_canframe_data_copied();
  test_filter_mask_value();
  test_load_fails_on_fresh_eeprom();
  test_save_load_roundtrip();
  test_save_masks_ids_to_10_bits();
  test_is_device_master_true();
  test_is_device_master_false();
  test_get_filtered_id();
  test_send_cmd_enum_dispatches();
  test_send_response_ack();
  test_send_response_nack();
  FINISH
}
