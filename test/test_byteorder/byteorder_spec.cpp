#include "byteOrder.hpp"
#include "BDDTest.h"

bool test_le16_takes_the_low_byte_first() {
  IT("le16() puts the first byte in the low half");
  IS_EQUAL(le16(0x34U, 0x12U), 0x1234U);
  END_IT
}

bool test_be16_takes_the_high_byte_first() {
  IT("be16() puts the first byte in the high half");
  IS_EQUAL(be16(0x12U, 0x34U), 0x1234U);
  END_IT
}

bool test_high_bit_survives() {
  IT("a high byte with its top bit set keeps every bit");
  IS_EQUAL(le16(0x00U, 0x80U), 0x8000U);
  IS_EQUAL(le16(0xFFU, 0xFFU), 0xFFFFU);
  END_IT
}

bool test_usable_at_compile_time() {
  IT("both evaluate in a constant expression");
  static_assert(le16(0xCDU, 0xABU) == 0xABCDU, "le16");
  static_assert(be16(0xABU, 0xCDU) == 0xABCDU, "be16");
  END_IT
}

int main() {
  SUITE("ByteOrder");
  test_le16_takes_the_low_byte_first();
  test_be16_takes_the_high_byte_first();
  test_high_bit_survives();
  test_usable_at_compile_time();
  FINISH
}
