#include "base64.hpp"

uint32_t Base64::encodedLength(uint32_t plainLength) {
  return (plainLength + 2 - ((plainLength + 2) % 3)) / 3 * 4;
}

uint32_t Base64::decodedLength(const uint8_t input[], uint32_t inputLength) {
  uint32_t numEq = 0;
  for(int32_t i = static_cast<int32_t>(inputLength) - 1; i >= 0 && input[i] == '='; i--) {
    numEq++;
  }
  return ((6 * inputLength) / 8) - numEq;
}

uint32_t Base64::encodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength, uint32_t outputLength) {
  if(outputLength < encodedLength(inputLength)) {
    return 0; // Output buffer too small
  }

  uint32_t i = 0;
  uint32_t encodedLength_ = 0;
  uint8_t A3[3];
  uint8_t A4[4];

  while(inputLength-- != 0U) {
    A3[i++] = *(input++);
    if(i == 3) {
      fromA3ToA4(A4, A3);
      for(i = 0; i < 4; i++) {
        output[encodedLength_++] = pgm_read_byte(&base64AlphabetTable[A4[i]]);
      }
      i = 0;
    }
  }
  if(i > 0) {
    for(uint32_t j = i; j < 3; j++) {
      A3[j] = '\0';
    }
    fromA3ToA4(A4, A3);
    for(uint32_t j = 0; j < i + 1; j++) {
      output[encodedLength_++] = pgm_read_byte(&base64AlphabetTable[A4[j]]);
    }
    while(i++ < 3) {
      output[encodedLength_++] = '=';
    }
  }
  if(encodedLength_ >= outputLength) {
    return 0; // Prevent overflow
  }
  output[encodedLength_] = '\0';
  return encodedLength_;
}

bool Base64::processFullBlock(uint8_t (&A4)[4], uint8_t output[], uint32_t &decodedLength) {
  uint8_t A3[3] = {0U};
  for(uint8_t &val : A4) {
    val = lookupTable(static_cast<char>(val));
    if(val == 255U) { return false; } // Invalid character
  }
  fromA4ToA3(A3, A4);
  for(uint8_t val : A3) {
    output[decodedLength++] = val;
  }
  return true;
}

bool Base64::processPartialBlock(uint8_t (&A4)[4], uint32_t count, uint8_t output[], uint32_t &decodedLength) {
  uint8_t A3[3] = {0U};
  for(uint32_t j = count; j < 4U; j++) { A4[j] = 0U; }
  for(uint8_t &val : A4) {
    if(val != 0U) { val = lookupTable(static_cast<char>(val)); }
    if(val == 255U) { return false; } // Invalid character
  }
  fromA4ToA3(A3, A4);
  for(uint32_t j = 0U; j < count - 1U; j++) {
    output[decodedLength++] = A3[j];
  }
  return true;
}

uint32_t Base64::decodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength, uint32_t outputLength) {
  uint32_t decodedLength_ = 0U;
  uint32_t i = 0U;
  uint8_t A4[4] = {0U};

  if(outputLength < decodedLength(input, inputLength)) {
    return 0U; // Output buffer too small
  }

  while(inputLength-- != 0U) {
    if(*input == '=') { break; }
    A4[i++] = *(input++);
    if(i == 4U) {
      if(!processFullBlock(A4, output, decodedLength_)) { return 0U; }
      i = 0U;
    }
  }
  if(i > 0U) {
    if(!processPartialBlock(A4, i, output, decodedLength_)) { return 0U; }
  }
  if(decodedLength_ >= outputLength) {
    return 0U; // Prevent overflow
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
  if(c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if(c >= 'a' && c <= 'z') {
    return static_cast<uint8_t>(c - 'a' + 26);
  }
  if(c >= '0' && c <= '9') {
    return static_cast<uint8_t>(c - '0' + 52);
  }
  if(c == '+') {
    return 62;
  }
  if(c == '/') {
    return 63;
  }
  return 255; // Invalid character
}