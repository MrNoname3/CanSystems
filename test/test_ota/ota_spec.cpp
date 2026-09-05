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

bool test_start_rejects_block_past_capacity() {
  IT("start returns false when the target block runs past the flash chip capacity");
  SPIFlash flash(0U); // host fake: 64 KB capacity = two 32 KB blocks (0 and 1)
  OTA ota(flash);
  // Block 2 begins at 64 KB, i.e. past the chip -> rejected before erasing anything.
  IS_FALSE(ota.start(2U, static_cast<uint32_t>(OTA::fwPieceSize), 0U));
  END_IT
}

bool test_start_rejects_when_capacity_unknown() {
  IT("start returns false when the flash reports an unknown (zero) capacity");
  SPIFlash flash(0U, 0U, 0U); // capacity 0 = absent/unreadable chip
  OTA ota(flash);
  IS_FALSE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), 0U));
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
  IS_TRUE(flash.writeByte(5U, 0xABU));
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

bool test_store_wrong_sequence_rejected() {
  IT("storeNextData returns false when the sequence is not the next expected one");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, 2U * OTA::fwPieceSize, 0U));
  uint8_t chunk[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  IS_FALSE(ota.storeNextData(OTA::fwPieceSize, chunk)); // the second piece, while the first is due
  END_IT
}

bool test_store_repeated_piece_rejected() {
  IT("a piece the device already stored is refused rather than written a second time");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, 3U * OTA::fwPieceSize, 0U));
  uint8_t chunk[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  IS_TRUE(ota.storeNextData(0U, chunk));
  // What a frame the bus delivered twice looks like: the sequence the device has just moved past.
  IS_FALSE(ota.storeNextData(0U, chunk));
  END_IT
}

bool test_store_programs_a_piece_in_one_command() {
  IT("a whole piece costs one program command, not one per byte");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, 3U * OTA::fwPieceSize, 0U));
  uint8_t chunk[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  IS_TRUE(ota.storeNextData(0U, chunk));                  // the piece holding the two kept bytes
  flash.clearProgramCommands();
  // The chip charges per program cycle, not per byte, and that cycle sits in the round trip the
  // sender waits on: a piece written byte by byte pays it once for every byte.
  IS_TRUE(ota.storeNextData(OTA::fwPieceSize, chunk));
  IS_EQUAL(flash.getProgramCommands(), 1U);
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
  IS_TRUE(ota.start(0U, OTA::fwPieceSize + 1U, 0U)); // one byte past a whole piece
  uint8_t chunk0[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  uint8_t chunk1[OTA::fwPieceSize] = { 0x05U, 0xC0U, 0xC0U, 0xC0U }; // only byte 0 is within fwSize
  IS_TRUE(ota.storeNextData(0U, chunk0));
  IS_TRUE(ota.storeNextData(OTA::fwPieceSize, chunk1));
  IS_EQUAL(flash.readByte(OTA::fwPieceSize), 0x05U);      // first byte of chunk1 was stored
  IS_EQUAL(flash.readByte(OTA::fwPieceSize + 1U), 0xFFU); // 0xC0 was NOT stored (beyond fwSize)
  END_IT
}

bool test_store_overflow_rejected() {
  IT("storeNextData returns false when all firmware bytes are already stored");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), 0U));
  uint8_t chunk[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  IS_TRUE(ota.storeNextData(0U, chunk));                // fills firmware exactly
  IS_FALSE(ota.storeNextData(OTA::fwPieceSize, chunk)); // overflow: flashPointer >= fwSize
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
  for(uint8_t i = 0U; i < OTA::fwPieceSize; i++) {
    IS_EQUAL(ota.run(), OtaState::CHECK); // STORE → CHECK, then one firmware byte per pass
  }
  IS_EQUAL(ota.run(), OtaState::VALID);  // last byte read, CRC OK, write-back → VALID
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
  for(uint8_t i = 0U; i < OTA::fwPieceSize; i++) {
    IS_EQUAL(ota.run(), OtaState::CHECK); // the transition, then one firmware byte per pass
  }
  IS_EQUAL(ota.run(), OtaState::INVALID); // CRC mismatch → INVALID (early return)
  IS_EQUAL(ota.run(), OtaState::IDLE);    // INVALID cleanup → IDLE
  END_IT
}

bool test_run_write_back_failure_goes_invalid() {
  IT("a failed write-back of the first two bytes ends the check as INVALID, not a stuck CHECK");
  SPIFlash flash(0U);
  OTA ota(flash);
  uint8_t fw[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  const uint16_t crc = Crc16::calculate(fw, static_cast<uint32_t>(OTA::fwPieceSize));
  IS_TRUE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), crc));
  IS_TRUE(ota.storeNextData(0U, fw));
  IS_EQUAL(ota.run(), OtaState::STORE);
  for(uint8_t i = 0U; i < OTA::fwPieceSize; i++) {
    IS_EQUAL(ota.run(), OtaState::CHECK); // the transition, then one firmware byte per pass
  }
  // The CRC is correct, so CHECK reaches the write-back of the two bytes it kept in RAM. That
  // write silently does nothing, so the read-back cannot match - the only signal the device gets.
  flash.setFailWrite(true);
  IS_EQUAL(ota.run(), OtaState::INVALID);
  IS_EQUAL(ota.run(), OtaState::IDLE);    // INVALID cleanup → IDLE
  END_IT
}

bool test_run_write_back_failure_does_not_loop() {
  IT("a failed write-back leaves the state machine, instead of rewriting flash on every pass");
  SPIFlash flash(0U);
  OTA ota(flash);
  uint8_t fw[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  const uint16_t crc = Crc16::calculate(fw, static_cast<uint32_t>(OTA::fwPieceSize));
  IS_TRUE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), crc));
  IS_TRUE(ota.storeNextData(0U, fw));
  flash.setFailWrite(true);
  // Far more passes than the image needs; a stuck CHECK would never leave the state machine.
  OtaState state = OtaState::IDLE;
  for(uint8_t i = 0U; i < 32U; i++) {
    state = ota.run();
  }
  IS_EQUAL(state, OtaState::IDLE);        // reached INVALID and cleaned up, rather than looping
  END_IT
}

bool test_start_rejects_when_erase_cannot_be_issued() {
  IT("start returns false when the chip is still busy with an earlier erase");
  SPIFlash flash(0U);
  OTA ota(flash);
  flash.setFailErase(true);
  IS_FALSE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), 0U));
  END_IT
}

bool test_store_reports_a_failed_flash_write() {
  IT("storeNextData fails on the piece the flash refused, instead of only at the closing CRC");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, 2U * OTA::fwPieceSize, 0U));
  uint8_t chunk[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  IS_TRUE(ota.storeNextData(0U, chunk));  // bytes 0-1 stay in RAM, the rest reach the flash
  flash.setFailWrite(true);
  IS_FALSE(ota.storeNextData(OTA::fwPieceSize, chunk)); // second piece is all flash, so the refusal shows up here
  IS_EQUAL(ota.run(), OtaState::IDLE);    // INVALID cleanup ran, rather than waiting for the CRC
  END_IT
}

bool test_run_read_back_failure_goes_invalid() {
  IT("a read-back the flash refuses ends the check as INVALID");
  SPIFlash flash(0U);
  OTA ota(flash);
  uint8_t fw[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  const uint16_t crc = Crc16::calculate(fw, static_cast<uint32_t>(OTA::fwPieceSize));
  IS_TRUE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), crc));
  IS_TRUE(ota.storeNextData(0U, fw));
  IS_EQUAL(ota.run(), OtaState::STORE);
  for(uint8_t i = 0U; i < OTA::fwPieceSize; i++) {
    IS_EQUAL(ota.run(), OtaState::CHECK); // the transition, then one firmware byte per pass
  }
  flash.setFailRead(true);                // the write lands, but the verification read does not
  IS_EQUAL(ota.run(), OtaState::INVALID);
  END_IT
}

bool test_run_store_stall_times_out() {
  IT("STORE ends as INVALID when no further piece arrives");
  setFakeMillis(0U);
  SPIFlash flash(0U);
  OTA ota(flash);
  uint8_t chunk[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  IS_TRUE(ota.start(0U, 2U * OTA::fwPieceSize, 0U)); // two pieces expected, only one will arrive
  IS_TRUE(ota.storeNextData(0U, chunk));
  IS_EQUAL(ota.run(), OtaState::STORE);
  setFakeMillis(4U * 60U * 1000U);        // > 3 min stall timeout
  IS_EQUAL(ota.run(), OtaState::INVALID);
  IS_EQUAL(ota.run(), OtaState::IDLE);
  clearFakeMillis();
  END_IT
}

bool test_run_start_stall_times_out() {
  IT("START ends as INVALID when the chip never reports itself ready");
  setFakeMillis(0U);
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), 0U));
  flash.setBusy(true);                    // erase that never finishes, e.g. an unresponsive chip
  IS_EQUAL(ota.run(), OtaState::START);
  setFakeMillis(4U * 60U * 1000U);
  IS_EQUAL(ota.run(), OtaState::INVALID);
  clearFakeMillis();
  END_IT
}

bool test_run_progress_keeps_the_transfer_alive() {
  IT("each accepted piece restarts the stall timeout, so a long transfer is not cut off");
  setFakeMillis(0U);
  SPIFlash flash(0U);
  OTA ota(flash);
  uint8_t chunk[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  IS_TRUE(ota.start(0U, 3U * OTA::fwPieceSize, 0U));
  // Pieces arrive 2 minutes apart: six minutes in total, none of the gaps past the timeout.
  for(uint8_t i = 0U; i < 3U; i++) {
    setFakeMillis((static_cast<uint32_t>(i) * 2U * 60U * 1000U) + 1U);
    IS_TRUE(ota.storeNextData(static_cast<uint8_t>(i * OTA::fwPieceSize), chunk));
    IS_TRUE(ota.run() != OtaState::INVALID);
  }
  IS_EQUAL(ota.run(), OtaState::CHECK);   // every byte in: moved on rather than timing out
  clearFakeMillis();
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
  for(uint8_t i = 0U; i < OTA::fwPieceSize; i++) {
    IS_EQUAL(ota.run(), OtaState::CHECK); // the transition, then one firmware byte per pass
  }
  IS_EQUAL(ota.run(), OtaState::INVALID);
  IS_EQUAL(ota.run(), OtaState::IDLE);    // INVALID: chipErase + reset → IDLE
  IS_EQUAL(flash.readByte(2U), 0xFFU);   // flash erased by INVALID handler
  END_IT
}

bool test_start_restart_clears_previous_session() {
  IT("calling start() again erases flash and resets the write pointer to 0");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), 0U));
  uint8_t chunk[OTA::fwPieceSize] = { 0xAAU, 0xBBU, 0xCCU, 0xDDU };
  IS_TRUE(ota.storeNextData(0U, chunk));
  IS_EQUAL(flash.readByte(2U), 0xCCU); // written in first session

  IS_TRUE(ota.start(0U, static_cast<uint32_t>(OTA::fwPieceSize), 0xFFFFU));
  IS_EQUAL(flash.readByte(2U), 0xFFU); // chipErase wiped it
  IS_TRUE(ota.storeNextData(0U, chunk)); // write pointer is back at 0
  END_IT
}

bool test_store_block1_writes_at_correct_flash_offset() {
  IT("storeNextData for block 1 writes bytes starting at the 32KB flash offset");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.start(1U, static_cast<uint32_t>(OTA::fwPieceSize), 0U));
  uint8_t chunk[OTA::fwPieceSize] = { 0xAAU, 0xBBU, 0xCCU, 0xDDU };
  IS_TRUE(ota.storeNextData(0U, chunk));
  IS_EQUAL(flash.readByte(32768U + 0U), 0xFFU); // first 2 bytes kept in OTA memory
  IS_EQUAL(flash.readByte(32768U + 1U), 0xFFU);
  IS_EQUAL(flash.readByte(32768U + 2U), 0xCCU); // written at block-1 offset
  IS_EQUAL(flash.readByte(32768U + 3U), 0xDDU);
  IS_EQUAL(flash.readByte(0U), 0xFFU); // block-0 untouched
  END_IT
}

bool test_run_full_valid_flow_block1() {
  IT("full store+CRC flow works correctly for block 1 (CHECK reads from 32KB offset)");
  SPIFlash flash(0U);
  OTA ota(flash);
  uint8_t fw[OTA::fwPieceSize] = { 0x01U, 0x02U, 0x03U, 0x04U };
  const uint16_t crc = Crc16::calculate(fw, static_cast<uint32_t>(OTA::fwPieceSize));
  IS_TRUE(ota.start(1U, static_cast<uint32_t>(OTA::fwPieceSize), crc));
  IS_TRUE(ota.storeNextData(0U, fw));
  IS_EQUAL(ota.run(), OtaState::STORE);
  for(uint8_t i = 0U; i < OTA::fwPieceSize; i++) {
    IS_EQUAL(ota.run(), OtaState::CHECK); // the transition, then one firmware byte per pass
  }
  IS_EQUAL(ota.run(), OtaState::VALID);
  IS_EQUAL(ota.run(), OtaState::IDLE);
  END_IT
}

// ---- isOwnFw() ----

bool test_is_own_fw_before_start() {
  IT("isOwnFw returns true on a fresh OTA object before start() is called");
  SPIFlash flash(0U);
  OTA ota(flash);
  IS_TRUE(ota.isOwnFw()); // flashBlockBeginAddress defaults to 0
  END_IT
}

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
  test_start_rejects_block_past_capacity();
  test_start_rejects_when_capacity_unknown();
  test_start_success();
  test_start_erases_flash();
  test_store_before_start_rejected();
  test_store_wrong_sequence_rejected();
  test_store_repeated_piece_rejected();
  test_store_programs_a_piece_in_one_command();
  test_store_first_two_bytes_in_memory_not_flash();
  test_store_partial_last_chunk();
  test_store_overflow_rejected();
  test_run_idle_stays_idle();
  test_run_start_stays_when_busy();
  test_run_full_valid_flow();
  test_run_crc_mismatch_goes_invalid();
  test_run_store_stall_times_out();
  test_run_start_stall_times_out();
  test_run_progress_keeps_the_transfer_alive();
  test_start_rejects_when_erase_cannot_be_issued();
  test_store_reports_a_failed_flash_write();
  test_run_read_back_failure_goes_invalid();
  test_run_write_back_failure_goes_invalid();
  test_run_write_back_failure_does_not_loop();
  test_run_invalid_clears_flash();
  test_start_restart_clears_previous_session();
  test_store_block1_writes_at_correct_flash_offset();
  test_run_full_valid_flow_block1();
  test_is_own_fw_before_start();
  test_is_own_fw_block_zero();
  test_is_own_fw_other_block();
  FINISH
}
