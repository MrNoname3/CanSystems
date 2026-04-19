#include "base64.hpp"
#include "BDDTest.h"
#include <string.h>

// Standard Base64 alphabet vectors (RFC 4648):
//   "Man"  -> "TWFu"
//   "Ma"   -> "TWE="
//   "M"    -> "TQ=="
//   "Hello" -> "SGVsbG8="

bool test_encoded_length() {
  IT("encodedLength returns correct padded output size");
  IS_EQUAL(Base64::encodedLength(0U),  0U);
  IS_EQUAL(Base64::encodedLength(1U),  4U);
  IS_EQUAL(Base64::encodedLength(2U),  4U);
  IS_EQUAL(Base64::encodedLength(3U),  4U);
  IS_EQUAL(Base64::encodedLength(4U),  8U);
  IS_EQUAL(Base64::encodedLength(6U),  8U);
  IS_EQUAL(Base64::encodedLength(9U), 12U);
  END_IT
}

bool test_decoded_length() {
  IT("decodedLength returns correct binary size from Base64 string");
  const uint8_t full[]    = {'T','W','F','u'};       // 0 padding -> 3
  const uint8_t onePad[]  = {'T','W','E','='};       // 1 padding -> 2
  const uint8_t twoPad[]  = {'T','Q','=','='};       // 2 padding -> 1
  IS_EQUAL(Base64::decodedLength(full,   4U), 3U);
  IS_EQUAL(Base64::decodedLength(onePad, 4U), 2U);
  IS_EQUAL(Base64::decodedLength(twoPad, 4U), 1U);
  END_IT
}

bool test_encode_three_byte_block() {
  IT("encodes a 3-byte block to 4 Base64 characters without padding");
  uint8_t out[8] = {};
  const uint8_t in[] = {'M','a','n'};
  IS_EQUAL(Base64::encodeBase64(in, out, 3U, sizeof(out)), 4U);
  IS_TRUE(strcmp(reinterpret_cast<char*>(out), "TWFu") == 0);
  END_IT
}

bool test_encode_two_byte_block() {
  IT("encodes a 2-byte block with one padding character");
  uint8_t out[8] = {};
  const uint8_t in[] = {'M','a'};
  IS_EQUAL(Base64::encodeBase64(in, out, 2U, sizeof(out)), 4U);
  IS_TRUE(strcmp(reinterpret_cast<char*>(out), "TWE=") == 0);
  END_IT
}

bool test_encode_one_byte_block() {
  IT("encodes a single byte with two padding characters");
  uint8_t out[8] = {};
  const uint8_t in[] = {'M'};
  IS_EQUAL(Base64::encodeBase64(in, out, 1U, sizeof(out)), 4U);
  IS_TRUE(strcmp(reinterpret_cast<char*>(out), "TQ==") == 0);
  END_IT
}

bool test_encode_hello() {
  IT("encodes 'Hello' to 'SGVsbG8='");
  uint8_t out[16] = {};
  const uint8_t in[] = {'H','e','l','l','o'};
  IS_EQUAL(Base64::encodeBase64(in, out, 5U, sizeof(out)), 8U);
  IS_TRUE(strcmp(reinterpret_cast<char*>(out), "SGVsbG8=") == 0);
  END_IT
}

bool test_decode_three_byte_block() {
  IT("decodes a full 4-character block to 3 bytes");
  uint8_t out[8] = {};
  const uint8_t in[] = {'T','W','F','u'};
  IS_EQUAL(Base64::decodeBase64(in, out, 4U, sizeof(out)), 3U);
  IS_TRUE(memcmp(out, "Man", 3U) == 0);
  END_IT
}

bool test_decode_two_byte_block() {
  IT("decodes a one-padded block to 2 bytes");
  uint8_t out[8] = {};
  const uint8_t in[] = {'T','W','E','='};
  IS_EQUAL(Base64::decodeBase64(in, out, 4U, sizeof(out)), 2U);
  IS_TRUE(memcmp(out, "Ma", 2U) == 0);
  END_IT
}

bool test_decode_one_byte_block() {
  IT("decodes a two-padded block to 1 byte");
  uint8_t out[8] = {};
  const uint8_t in[] = {'T','Q','=','='};
  IS_EQUAL(Base64::decodeBase64(in, out, 4U, sizeof(out)), 1U);
  IS_EQUAL(out[0], static_cast<uint8_t>('M'));
  END_IT
}

bool test_roundtrip_binary_data() {
  IT("encode then decode returns the original binary data");
  const uint8_t original[] = {0x00U, 0x01U, 0x7FU, 0x80U, 0xFFU};
  uint8_t encoded[16] = {};
  uint8_t decoded[16] = {};

  uint32_t encLen = Base64::encodeBase64(original, encoded, 5U, sizeof(encoded));
  IS_EQUAL(encLen, 8U);

  uint32_t decLen = Base64::decodeBase64(encoded, decoded, encLen, sizeof(decoded));
  IS_EQUAL(decLen, 5U);
  IS_TRUE(memcmp(decoded, original, 5U) == 0);
  END_IT
}

bool test_encode_output_buffer_too_small() {
  IT("encode returns 0 when output buffer is too small");
  const uint8_t in[] = {'M','a','n'};
  uint8_t out[3] = {}; // needs at least 5 (4 chars + null)
  IS_EQUAL(Base64::encodeBase64(in, out, 3U, 3U), 0U);
  END_IT
}

bool test_decode_output_buffer_too_small() {
  IT("decode returns 0 when output buffer is too small");
  const uint8_t in[] = {'T','W','F','u'};
  uint8_t out[2] = {}; // needs at least 4 (3 bytes + null)
  IS_EQUAL(Base64::decodeBase64(in, out, 4U, 2U), 0U);
  END_IT
}

bool test_decode_invalid_character() {
  IT("decode returns 0 on invalid Base64 character");
  const uint8_t in[] = {'T','!','F','u'}; // '!' is not valid Base64
  uint8_t out[8] = {};
  IS_EQUAL(Base64::decodeBase64(in, out, 4U, sizeof(out)), 0U);
  END_IT
}

int main() {
  SUITE("Base64");
  test_encoded_length();
  test_decoded_length();
  test_encode_three_byte_block();
  test_encode_two_byte_block();
  test_encode_one_byte_block();
  test_encode_hello();
  test_decode_three_byte_block();
  test_decode_two_byte_block();
  test_decode_one_byte_block();
  test_roundtrip_binary_data();
  test_encode_output_buffer_too_small();
  test_decode_output_buffer_too_small();
  test_decode_invalid_character();
  FINISH
}
