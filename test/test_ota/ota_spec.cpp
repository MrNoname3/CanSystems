#include "ota.hpp"
#include "BDDTest.h"

using OtaState = OTA::OtaState;

// ---- start() ----

bool test_start_rejects_zero_size() {
  IT("start returns false when fwSize is zero");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_FALSE(ota.start(0U, 0U, 0U));
  END_IT
}

bool test_start_rejects_oversized_fw() {
  IT("start returns false when fwSize exceeds program memory size");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_FALSE(ota.start(0U, static_cast<uint32_t>(PROGRAM_MEMORY_SIZE) + 1U, 0U));
  END_IT
}

bool test_start_success() {
  IT("start returns true for a valid fwSize");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, 4U, 0U));
  END_IT
}

bool test_start_erases_flash() {
  IT("start calls chipErase so pre-existing flash bytes read back as 0xFF");
  SPIFlash flash(0U);
  OTA ota(flash);
  flash.writeByte(5U, 0xABU);
  IS_EQUAL(flash.readByte(5U), 0xABU);
  IS_TRUE(ota.start(0U, 4U, 0U));
  IS_EQUAL(flash.readByte(5U), 0xFFU);
  END_IT
}

// ---- storeNextData() ----

bool test_store_before_start_rejected() {
  IT("storeNextData returns false when start has not been called (fwSize is 0)");
  SPIFlash flash(0U);
  OTA ota(flash);
  uint8_t chunk[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  IS_FALSE(ota.storeNextData(0U, chunk));
  END_IT
}

bool test_store_wrong_address_rejected() {
  IT("storeNextData returns false when the address is not the next expected one");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, 8U, 0U));
  uint8_t chunk[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  IS_FALSE(ota.storeNextData(4U, chunk)); // expected 0, got 4
  END_IT
}

bool test_store_first_two_bytes_in_memory_not_flash() {
  IT("first two bytes stay in OTA memory; bytes 2+ are written to flash");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), 0U));
  uint8_t chunk[OTA::fwPieceSize] = { 0xAAU, 0xBBU, 0xCCU, 0xDDU };
  IS_TRUE(ota.storeNextData(0U, chunk));
  IS_EQUAL(flash.readByte(0U), 0xFFU); // byte 0: kept in OTA memory, not flash
  IS_EQUAL(flash.readByte(1U), 0xFFU); // byte 1: kept in OTA memory, not flash
  IS_EQUAL(flash.readByte(2U), 0xCCU); // byte 2: written to flash
  IS_EQUAL(flash.readByte(3U), 0xDDU); // byte 3: written to flash
  END_IT
}

bool test_store_partial_last_chunk() {
  IT("storeNextData writes only the remaining bytes for the final partial chunk");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, 5U, 0U)); // 5 is not a multiple of fwPieceSize (4)
  uint8_t chunk0[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  uint8_t chunk1[OTA::fwPieceSize] = { 0x05U, 0xC0U, 0xC0U, 0xC0U }; // only byte 0 is within fwSize
  IS_TRUE(ota.storeNextData(0U, chunk0));
  IS_TRUE(ota.storeNextData(4U, chunk1));
  IS_EQUAL(flash.readByte(4U), 0x05U); // first byte of chunk1 was stored
  IS_EQUAL(flash.readByte(5U), 0xFFU); // 0xC0 was NOT stored (beyond fwSize)
  END_IT
}

bool test_store_overflow_rejected() {
  IT("storeNextData returns false when all firmware bytes are already stored");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), 0U));
  uint8_t chunk[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  IS_TRUE(ota.storeNextData(0U, chunk));  // fills firmware exactly
  IS_FALSE(ota.storeNextData(4U, chunk)); // overflow: flashPointer >= fwSize
  END_IT
}

// ---- run() state machine ----

bool test_run_idle_stays_idle() {
  IT("run in IDLE state returns IDLE without state change");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_EQUAL(ota.run(), OtaState::IDLE);
  IS_EQUAL(ota.run(), OtaState::IDLE);
  END_IT
}

bool test_run_start_stays_when_busy() {
  IT("START state stays when flash is busy and transitions to STORE when not busy");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, 4U, 0U));
  flash.setBusy(true);
  IS_EQUAL(ota.run(), OtaState::START);  // busy: stays START
  flash.setBusy(false);
  IS_EQUAL(ota.run(), OtaState::STORE);  // not busy: transitions to STORE
  END_IT
}

bool test_run_full_valid_flow() {
  IT("complete store+CRC flow transitions STORE→CHECK→VALID→IDLE");
  SPIFlash flash(0U);
  OTA ota(flash);
  uint8_t fw[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  const uint16_t crc = Crc16::calculate(fw, static_cast<uint32_t>(OTA::fwPieceSize));
  IS_TRUE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), crc));
  IS_TRUE(ota.storeNextData(0U, fw));
  IS_EQUAL(ota.run(), OtaState::STORE);  // START → STORE
  IS_EQUAL(ota.run(), OtaState::CHECK);  // STORE → CHECK
  IS_EQUAL(ota.run(), OtaState::CHECK);  // CHECK: read byte 0
  IS_EQUAL(ota.run(), OtaState::CHECK);  // CHECK: read byte 1
  IS_EQUAL(ota.run(), OtaState::CHECK);  // CHECK: read byte 2
  IS_EQUAL(ota.run(), OtaState::VALID);  // CHECK: read byte 3, CRC OK, write-back → VALID
  IS_EQUAL(ota.run(), OtaState::IDLE);   // VALID → IDLE
  END_IT
}

bool test_run_crc_mismatch_goes_invalid() {
  IT("wrong CRC causes CHECK to transition to INVALID");
  SPIFlash flash(0U);
  OTA ota(flash);
  uint8_t fw[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  const uint16_t correctCrc = Crc16::calculate(fw, static_cast<uint32_t>(OTA::fwPieceSize));
  IS_TRUE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), correctCrc + 1U));
  IS_TRUE(ota.storeNextData(0U, fw));
  IS_EQUAL(ota.run(), OtaState::STORE);
  IS_EQUAL(ota.run(), OtaState::CHECK);
  IS_EQUAL(ota.run(), OtaState::CHECK);
  IS_EQUAL(ota.run(), OtaState::CHECK);
  IS_EQUAL(ota.run(), OtaState::CHECK);
  IS_EQUAL(ota.run(), OtaState::INVALID); // CRC mismatch → INVALID (early return)
  IS_EQUAL(ota.run(), OtaState::IDLE);    // INVALID cleanup → IDLE
  END_IT
}

bool test_run_invalid_clears_flash() {
  IT("INVALID state calls chipErase so flash bytes return to the erased 0xFF state");
  SPIFlash flash(0U);
  OTA ota(flash);
  uint8_t fw[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  const uint16_t correctCrc = Crc16::calculate(fw, static_cast<uint32_t>(OTA::fwPieceSize));
  IS_TRUE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), correctCrc + 1U));
  IS_TRUE(ota.storeNextData(0U, fw));
  IS_EQUAL(flash.readByte(2U), 0x03U); // byte 2 was written to flash by storeNextData
  IS_EQUAL(ota.run(), OtaState::STORE);
  IS_EQUAL(ota.run(), OtaState::CHECK);
  IS_EQUAL(ota.run(), OtaState::CHECK);
  IS_EQUAL(ota.run(), OtaState::CHECK);
  IS_EQUAL(ota.run(), OtaState::CHECK);
  IS_EQUAL(ota.run(), OtaState::INVALID);
  IS_EQUAL(ota.run(), OtaState::IDLE);    // INVALID: chipErase + reset → IDLE
  IS_EQUAL(flash.readByte(2U), 0xFFU);   // flash erased by INVALID handler
  END_IT
}

// ---- isOwnFw() ----

bool test_is_own_fw_block_zero() {
  IT("isOwnFw returns true when firmware is stored in flash block 0");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, 4U, 0U));
  IS_TRUE(ota.isOwnFw());
  END_IT
}

bool test_is_own_fw_other_block() {
  IT("isOwnFw returns false when firmware is stored in flash block other than 0");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(1U, 4U, 0U));
  IS_FALSE(ota.isOwnFw());
  END_IT
}

int main() {
  SUITE("OTA");
  test_start_rejects_zero_size();
  test_start_rejects_oversized_fw();
  test_start_success();
  test_start_erases_flash();
  test_store_before_start_rejected();
  test_store_wrong_address_rejected();
  test_store_first_two_bytes_in_memory_not_flash();
  test_store_partial_last_chunk();
  test_store_overflow_rejected();
  test_run_idle_stays_idle();
  test_run_start_stays_when_busy();
  test_run_full_valid_flow();
  test_run_crc_mismatch_goes_invalid();
  test_run_invalid_clears_flash();
  test_is_own_fw_block_zero();
  test_is_own_fw_other_block();
  FINISH
}
