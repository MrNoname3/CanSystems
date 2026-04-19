#include "crc16.hpp"
#include "BDDTest.h"

// Standard CRC16-XModem test vector: "123456789" -> 0x31C3

bool test_calculate_known_vector() {
  IT("computes known CRC16-XModem vector for '123456789'");
  const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
  IS_EQUAL(Crc16::calculate(data, 9U), 0x31C3U);
  END_IT
}

bool test_calculate_empty_returns_init() {
  IT("returns init value for null or zero-length input");
  const uint8_t data[] = {0x01U};
  IS_EQUAL(Crc16::calculate(nullptr, 0U), 0U);
  IS_EQUAL(Crc16::calculate(nullptr, 5U), 0U);
  IS_EQUAL(Crc16::calculate(data, 0U), 0U);
  END_IT
}

bool test_incremental_matches_batch() {
  IT("incremental next() matches single calculate() call");
  const uint8_t data[] = {0x01U, 0x02U, 0x03U, 0x04U};
  uint16_t batch = Crc16::calculate(data, 4U);

  Crc16 crc;
  for (const uint8_t b : data) {
    crc.next(b);
  }
  IS_EQUAL(crc.get(), batch);
  END_IT
}

bool test_array_next_matches_single_bytes() {
  IT("next(array, len) matches sequential next(byte) calls");
  const uint8_t data[] = {'H','e','l','l','o'};

  Crc16 crc1;
  crc1.next(data, 5U);

  Crc16 crc2;
  for (const uint8_t b : data) {
    crc2.next(b);
  }
  IS_EQUAL(crc1.get(), crc2.get());
  END_IT
}

bool test_reset_restores_initial_value() {
  IT("reset restores CRC to initial value");
  Crc16 crc;
  crc.next(0xAAU);
  crc.next(0xBBU);
  crc.reset();
  IS_EQUAL(crc.get(), 0U);

  const uint8_t data[] = {0xAAU, 0xBBU};
  crc.next(data, 2U);
  IS_EQUAL(crc.get(), Crc16::calculate(data, 2U));
  END_IT
}

bool test_verify_correct_crc() {
  IT("verify returns true for correct CRC");
  const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
  IS_TRUE(Crc16::verify(data, 9U, 0x31C3U));
  IS_FALSE(Crc16::verify(data, 9U, 0x0000U));
  END_IT
}

bool test_verify_empty_data() {
  IT("verify handles null/empty data correctly");
  IS_TRUE(Crc16::verify(nullptr, 0U, 0U));
  IS_FALSE(Crc16::verify(nullptr, 0U, 0x1234U));
  END_IT
}

bool test_custom_init_value() {
  IT("custom init value produces different CRC than default");
  const uint8_t data[] = {0x01U, 0x02U};
  uint16_t crcDefault = Crc16::calculate(data, 2U, 0x0000U);
  uint16_t crcCustom  = Crc16::calculate(data, 2U, 0xFFFFU);
  IS_NOT_EQUAL(crcDefault, crcCustom);

  Crc16 crc(0xFFFFU);
  crc.next(data, 2U);
  IS_EQUAL(crc.get(), crcCustom);
  END_IT
}

bool test_next_ignores_null_array() {
  IT("next(nullptr, n) does nothing to CRC state");
  Crc16 crc;
  crc.next(nullptr, 5U);
  IS_EQUAL(crc.get(), 0U);
  END_IT
}

bool test_two_segment_continuity() {
  IT("CRC computed in two segments matches one-shot calculation");
  const uint8_t full[] = {0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U};
  uint16_t expected = Crc16::calculate(full, 6U);

  Crc16 crc;
  crc.next(full, 3U);
  crc.next(full + 3U, 3U);
  IS_EQUAL(crc.get(), expected);
  END_IT
}

int main() {
  SUITE("Crc16");
  test_calculate_known_vector();
  test_calculate_empty_returns_init();
  test_incremental_matches_batch();
  test_array_next_matches_single_bytes();
  test_reset_restores_initial_value();
  test_verify_correct_crc();
  test_verify_empty_data();
  test_custom_init_value();
  test_next_ignores_null_array();
  test_two_segment_continuity();
  FINISH
}
