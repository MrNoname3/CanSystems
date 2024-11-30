#include "base64.hpp"

uint32_t Base64::encodedLength(uint32_t plainLength) {
  int32_t n = plainLength;
  return (n + 2 - ((n + 2) % 3)) / 3 * 4;
}

uint32_t Base64::decodedLength(const uint8_t input[], uint32_t inputLength) {
  int32_t i = 0;
  int32_t numEq = 0;
  for(i = inputLength - 1; input[i] == '='; i--) { numEq++; }
  return ((6 * inputLength) / 8) - numEq;
}

uint32_t Base64::encodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength) {
  int32_t i = 0;
  int32_t encodedLength_ = 0;
  uint8_t A3[3];
  uint8_t A4[4];

  while(inputLength--) {
    A3[i++] = *(input++);
    if(i == 3) {
      fromA3ToA4(A4, A3);
      for(i = 0; i < 4; i++) {
        output[encodedLength_++] = pgm_read_byte(&base64AlphabetTable_[A4[i]]);
      }
      i = 0;
    }
  }
  if(i) {
    int32_t j = 0;
    for(j = i; j < 3; j++) {
      A3[j] = '\0';
    }
    fromA3ToA4(A4, A3);
    for(j = 0; j < i + 1; j++) {
      output[encodedLength_++] = pgm_read_byte(&base64AlphabetTable_[A4[j]]);
    }
    while((i++ < 3)) {
      output[encodedLength_++] = '=';
    }
  }
  output[encodedLength_] = '\0';
  return encodedLength_;
}

uint32_t Base64::decodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength) {
  int32_t i = 0;
  uint32_t decodedLength_ = 0;
  uint8_t A3[3];
  uint8_t A4[4];

  while(inputLength--) {
    if(*input == '=') { break; }
    A4[i++] = *(input++);
    if(i == 4) {
      for(i = 0; i < 4; i++) {
        A4[i] = lookupTable(A4[i]);
      }
      fromA4ToA3(A3, A4);
      for(i = 0; i < 3; i++) {
        output[decodedLength_++] = A3[i];
      }
      i = 0;
    }
  }
  if(i) {
    int32_t j = 0;
    for(j = i; j < 4; j++) {
      A4[j] = '\0';
    }
    for(j = 0; j < 4; j++) {
      A4[j] = lookupTable(A4[j]);
    }
    fromA4ToA3(A3, A4);
    for(j = 0; j < i - 1; j++) {
      output[decodedLength_++] = A3[j];
    }
  }
  output[decodedLength_] = '\0';
  return decodedLength_;
}

void Base64::fromA3ToA4(uint8_t* A4, const uint8_t* A3) {
  A4[0] = (A3[0] & 0xfc) >> 2;
  A4[1] = ((A3[0] & 0x03) << 4) + ((A3[1] & 0xf0) >> 4);
  A4[2] = ((A3[1] & 0x0f) << 2) + ((A3[2] & 0xc0) >> 6);
  A4[3] = (A3[2] & 0x3f);
}

void Base64::fromA4ToA3(uint8_t* A3, const uint8_t* A4) {
  A3[0] = (A4[0] << 2) + ((A4[1] & 0x30) >> 4);
  A3[1] = ((A4[1] & 0xf) << 4) + ((A4[2] & 0x3c) >> 2);
  A3[2] = ((A4[2] & 0x3) << 6) + A4[3];
}

uint8_t Base64::lookupTable(char c) {
  if(c >='A' && c <='Z') { return c - 'A'; }
  if(c >='a' && c <='z') { return c - 71; }
  if(c >='0' && c <='9') { return c + 4; }
  if(c == '+') { return 62; }
  if(c == '/') { return 63; }
  return -1;
}

const char Base64::base64AlphabetTable_[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789+/";