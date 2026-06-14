#include "otaCanFrame.hpp"
#include "otaCanResponse.hpp"
#include "ota.hpp"
#include "crc16.hpp"
#include "BDDTest.h"

// This suite pins the OTA-over-CAN wire format (OtaCanFrame) and exercises the device-side
// parse path that the AVR firmware (canHandlerAtmega328P) runs but that is excluded from the
// native build by #ifdef ARDUINO_ARCH_AVR. By driving a real OTA storage object through the
// shared unpack helpers we cover that glue on the host, and prove a packed frame round-trips
// back into the exact fields OTA::start()/storeNextData() expect.

// ---- byte-layout (the contract, pinned) ----

bool test_start_frame_byte_layout() {
  IT("packStart lays the OTA_START fields out little-endian in the 8 CAN bytes");
  OtaCanFrame::StartFrame fields;
  fields.storageNumber = 0x0201U;
  fields.fwSize = 0x06050403UL;
  fields.fwCrc = 0x0807U;
  uint8_t canData[8] = { 0U };
  OtaCanFrame::packStart(fields, canData);
  IS_EQUAL(canData[0], 0x01U);                       // storageNumber low
  IS_EQUAL(canData[1], 0x02U);                       // storageNumber high
  IS_EQUAL(canData[2], 0x03U);                       // fwSize byte 0
  IS_EQUAL(canData[3], 0x04U);                       // fwSize byte 1
  IS_EQUAL(canData[4], 0x05U);                       // fwSize byte 2
  IS_EQUAL(canData[5], 0x06U);                       // fwSize byte 3
  IS_EQUAL(canData[6], 0x07U);                       // fwCrc low
  IS_EQUAL(canData[7], 0x08U);                       // fwCrc high
  END_IT
}

bool test_send_frame_byte_layout() {
  IT("packSend lays the data piece in bytes 0-3 and the offset little-endian in bytes 4-7");
  OtaCanFrame::SendFrame fields;
  fields.data[0] = 0xAAU;
  fields.data[1] = 0xBBU;
  fields.data[2] = 0xCCU;
  fields.data[3] = 0xDDU;
  fields.dataAddress = 0x44332211UL;
  uint8_t canData[8] = { 0U };
  OtaCanFrame::packSend(fields, canData);
  IS_EQUAL(canData[0], 0xAAU);
  IS_EQUAL(canData[1], 0xBBU);
  IS_EQUAL(canData[2], 0xCCU);
  IS_EQUAL(canData[3], 0xDDU);
  IS_EQUAL(canData[4], 0x11U);                       // dataAddress byte 0
  IS_EQUAL(canData[5], 0x22U);                       // dataAddress byte 1
  IS_EQUAL(canData[6], 0x33U);                       // dataAddress byte 2
  IS_EQUAL(canData[7], 0x44U);                       // dataAddress byte 3
  END_IT
}

// ---- round-trip (pack -> unpack restores every field, including the extremes) ----

bool test_start_frame_round_trip() {
  IT("unpackStart restores the fields packStart wrote, across the full value range");
  OtaCanFrame::StartFrame in;
  in.storageNumber = 0xFFFFU;
  in.fwSize = 0xFFFFFFFFUL;
  in.fwCrc = 0xABCDU;
  uint8_t canData[8] = { 0U };
  OtaCanFrame::packStart(in, canData);
  const OtaCanFrame::StartFrame out = OtaCanFrame::unpackStart(canData);
  IS_EQUAL(out.storageNumber, 0xFFFFU);
  IS_EQUAL(out.fwSize, 0xFFFFFFFFUL);
  IS_EQUAL(out.fwCrc, 0xABCDU);
  END_IT
}

bool test_send_frame_round_trip() {
  IT("unpackSend restores the data piece and the byte offset packSend wrote");
  OtaCanFrame::SendFrame in;
  in.data[0] = 0x12U;
  in.data[1] = 0x34U;
  in.data[2] = 0x56U;
  in.data[3] = 0x78U;
  in.dataAddress = 0xFFFFFFFFUL;
  uint8_t canData[8] = { 0U };
  OtaCanFrame::packSend(in, canData);
  const OtaCanFrame::SendFrame out = OtaCanFrame::unpackSend(canData);
  IS_EQUAL(out.data[0], 0x12U);
  IS_EQUAL(out.data[1], 0x34U);
  IS_EQUAL(out.data[2], 0x56U);
  IS_EQUAL(out.data[3], 0x78U);
  IS_EQUAL(out.dataAddress, 0xFFFFFFFFUL);
  END_IT
}

// ---- device-side parse glue driving real OTA storage ----

// Mirrors the AVR canHandlerAtmega328P OTA_START/OTA_SEND handlers: take the bytes a gateway
// would put on the wire, unpack them with the shared helpers, and feed a real OTA object.
// Returns the terminal storage state (VALID / INVALID) so a test can assert the outcome.
static OTA::OtaState streamThroughDevice(OTA& ota, const uint8_t* fw, uint32_t fwSize, uint16_t fwCrc) {
  OtaCanFrame::StartFrame startFields;
  startFields.storageNumber = 0U;
  startFields.fwSize = fwSize;
  startFields.fwCrc = fwCrc;
  uint8_t startData[8] = { 0U };
  OtaCanFrame::packStart(startFields, startData);
  const OtaCanFrame::StartFrame parsedStart = OtaCanFrame::unpackStart(startData);
  if(!ota.start(parsedStart.storageNumber, parsedStart.fwSize, parsedStart.fwCrc)) {
    return OTA::OtaState::INVALID;
  }

  uint32_t offset = 0U;
  while(offset < fwSize) {
    const uint32_t remaining = fwSize - offset;
    const uint8_t pieceLength = (remaining >= OtaCanFrame::dataPieceSize) ? OtaCanFrame::dataPieceSize : static_cast<uint8_t>(remaining);
    OtaCanFrame::SendFrame sendFields;
    for(uint8_t i = 0U; i < pieceLength; i++) {
      sendFields.data[i] = fw[offset + i];
    }
    sendFields.dataAddress = offset;
    uint8_t sendData[8] = { 0U };
    OtaCanFrame::packSend(sendFields, sendData);
    const OtaCanFrame::SendFrame parsedSend = OtaCanFrame::unpackSend(sendData);
    if(!ota.storeNextData(parsedSend.dataAddress, parsedSend.data)) {
      return OTA::OtaState::INVALID;
    }
    offset += pieceLength;
  }

  for(int i = 0; i < 256; i++) {
    const OTA::OtaState state = ota.run();
    if(state == OTA::OtaState::VALID || state == OTA::OtaState::INVALID) { return state; }
  }
  return OTA::OtaState::IDLE;
}

bool test_device_parse_reconstructs_firmware() {
  IT("packed frames parsed by the device reconstruct the firmware byte-for-byte and validate");
  SPIFlash flash(0U);
  OTA ota(flash);
  // 10 bytes: not a multiple of the 4-byte piece size, so the last piece is partial.
  const uint8_t fw[10] = { 0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U, 0x70U, 0x80U, 0x90U, 0xA0U };
  const uint16_t crc = Crc16::calculate(fw, sizeof(fw));
  IS_EQUAL(streamThroughDevice(ota, fw, sizeof(fw), crc), OTA::OtaState::VALID);
  // The first two bytes live in OTA memory until VALID writes them back; the rest came via flash.
  for(size_t i = 0U; i < sizeof(fw); i++) {
    IS_EQUAL(flash.readByte(static_cast<uint32_t>(i)), fw[i]);
  }
  END_IT
}

bool test_device_parse_crc_mismatch_is_invalid() {
  IT("a frame stream whose declared CRC disagrees with the bytes ends INVALID, not VALID");
  SPIFlash flash(0U);
  OTA ota(flash);
  const uint8_t fw[8] = { 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U };
  const uint16_t wrongCrc = static_cast<uint16_t>(Crc16::calculate(fw, sizeof(fw)) + 1U);
  IS_EQUAL(streamThroughDevice(ota, fw, sizeof(fw), wrongCrc), OTA::OtaState::INVALID);
  END_IT
}

bool test_device_parse_rejects_wrong_offset() {
  IT("an OTA_SEND frame carrying an out-of-sequence offset is rejected by storeNextData");
  SPIFlash flash(0U);
  OTA ota(flash);
  const uint8_t fw[8] = { 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U };
  IS_TRUE(ota.start(0U, sizeof(fw), 0U));
  // Craft a SEND frame for offset 4 while the device still expects offset 0.
  OtaCanFrame::SendFrame sendFields;
  sendFields.data[0] = fw[4];
  sendFields.data[1] = fw[5];
  sendFields.data[2] = fw[6];
  sendFields.data[3] = fw[7];
  sendFields.dataAddress = 4U;
  uint8_t sendData[8] = { 0U };
  OtaCanFrame::packSend(sendFields, sendData);
  const OtaCanFrame::SendFrame parsed = OtaCanFrame::unpackSend(sendData);
  IS_EQUAL(parsed.dataAddress, 4U);
  IS_FALSE(ota.storeNextData(parsed.dataAddress, parsed.data));   // device would answer NACK
  END_IT
}

// ---- device-side OTA response decision (OtaCanResponse::decide) ----
// The decision the AVR run() loop makes on each OTA storage transition: which CAN response to send
// back to the gateway and whether to reboot. The loop itself is AVR-only; this pins the logic.

bool test_decide_ack_start_on_store_entry() {
  IT("entering STORE from START requests an OTA_START ACK and no reboot");
  const OtaCanResponse::Decision decision = OtaCanResponse::decide(OTA::OtaState::START, OTA::OtaState::STORE, true);
  IS_TRUE(decision.action == OtaCanResponse::Action::ACK_START);
  IS_FALSE(decision.reboot);
  END_IT
}

bool test_decide_ack_end_and_reboot_for_own_fw() {
  IT("a VALID update that targets this device ACKs OTA_END and reboots");
  const OtaCanResponse::Decision decision = OtaCanResponse::decide(OTA::OtaState::CHECK, OTA::OtaState::VALID, true);
  IS_TRUE(decision.action == OtaCanResponse::Action::ACK_END);
  IS_TRUE(decision.reboot);
  END_IT
}

bool test_decide_ack_end_no_reboot_for_other_fw() {
  IT("a VALID update for another device ACKs OTA_END but does NOT reboot this one");
  const OtaCanResponse::Decision decision = OtaCanResponse::decide(OTA::OtaState::CHECK, OTA::OtaState::VALID, false);
  IS_TRUE(decision.action == OtaCanResponse::Action::ACK_END);
  IS_FALSE(decision.reboot);
  END_IT
}

bool test_decide_nack_end_on_invalid() {
  IT("an INVALID outcome NACKs OTA_END and never reboots");
  const OtaCanResponse::Decision decision = OtaCanResponse::decide(OTA::OtaState::CHECK, OTA::OtaState::INVALID, true);
  IS_TRUE(decision.action == OtaCanResponse::Action::NACK_END);
  IS_FALSE(decision.reboot);
  END_IT
}

bool test_decide_none_mid_transfer() {
  IT("staying in STORE mid-transfer emits no response");
  const OtaCanResponse::Decision decision = OtaCanResponse::decide(OTA::OtaState::STORE, OTA::OtaState::STORE, true);
  IS_TRUE(decision.action == OtaCanResponse::Action::NONE);
  IS_FALSE(decision.reboot);
  END_IT
}

bool test_decide_none_while_flash_busy() {
  IT("staying in START while the flash is still busy emits no response");
  const OtaCanResponse::Decision decision = OtaCanResponse::decide(OTA::OtaState::START, OTA::OtaState::START, true);
  IS_TRUE(decision.action == OtaCanResponse::Action::NONE);
  IS_FALSE(decision.reboot);
  END_IT
}

bool test_decide_none_when_idle() {
  IT("an idle state machine emits no response");
  const OtaCanResponse::Decision decision = OtaCanResponse::decide(OTA::OtaState::IDLE, OTA::OtaState::IDLE, false);
  IS_TRUE(decision.action == OtaCanResponse::Action::NONE);
  IS_FALSE(decision.reboot);
  END_IT
}

int main() {
  SUITE("OtaCanFrame");
  test_start_frame_byte_layout();
  test_send_frame_byte_layout();
  test_start_frame_round_trip();
  test_send_frame_round_trip();
  test_device_parse_reconstructs_firmware();
  test_device_parse_crc_mismatch_is_invalid();
  test_device_parse_rejects_wrong_offset();
  test_decide_ack_start_on_store_entry();
  test_decide_ack_end_and_reboot_for_own_fw();
  test_decide_ack_end_no_reboot_for_other_fw();
  test_decide_nack_end_on_invalid();
  test_decide_none_mid_transfer();
  test_decide_none_while_flash_busy();
  test_decide_none_when_idle();
  FINISH
}
