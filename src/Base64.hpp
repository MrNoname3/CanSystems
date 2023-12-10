#ifndef BASE64_HPP
#define BASE64_HPP

#include <Arduino.h>                          /// Arduino libraries header.
#if (defined(__AVR__) || defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_SAM))
#include <avr/pgmspace.h>
#else
#include <pgmspace.h>
#endif

/// @brief Base64 encoding and decoding of strings. Uses '+' for 62, '/' for 63, '=' for padding.
class Base64 {
public:
  Base64() = delete;
  ~Base64() = delete;

public:
  /// @brief Calculates length of base64 string needed for a given number of binary bytes.
  /// @param plainLength Amount of binary data in bytes.
  /// @return Number of base64 characters needed to encode input_length bytes of binary data.
  static uint32_t encodedLength(uint32_t plainLength) {
    int32_t n = plainLength;
    return (n + 2 - ((n + 2) % 3)) / 3 * 4;
  }

  /// @brief Calculates number of bytes of binary data in a base64 string.
  /// @param input Base64-encoded null-terminated string.
  /// @param inputLength Number of bytes to read from input pointer.
  /// @return Number of bytes of binary data in input.
  static uint32_t decodedLength(const uint8_t input[], uint32_t inputLength) {
    int32_t i = 0;
    int32_t numEq = 0;
    for(i = inputLength - 1; input[i] == '='; i--) { numEq++; }
    return ((6 * inputLength) / 8) - numEq;
  }

  /// @brief Converts an array of bytes to a base64 null-terminated string.
  /// @param input Pointer to input data.
  /// @param output Pointer to output string. Null terminator will be added automatically.
  /// @param inputLength Number of bytes to read from input pointer.
  /// @return Length of encoded string in bytes (not including null terminator).
  static uint32_t encodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength) {
    int32_t i = 0, j = 0;
    int32_t encodedLength = 0;
    uint8_t A3[3];
    uint8_t A4[4];

    while(inputLength--) {
      A3[i++] = *(input++);
      if(i == 3) {
        fromA3ToA4(A4, A3);
        for(i = 0; i < 4; i++) {
          output[encodedLength++] = pgm_read_byte(&_Base64AlphabetTable[A4[i]]);
        }
        i = 0;
      }
    }
    if(i) {
      for(j = i; j < 3; j++) {
        A3[j] = '\0';
      }
      fromA3ToA4(A4, A3);
      for(j = 0; j < i + 1; j++) {
        output[encodedLength++] = pgm_read_byte(&_Base64AlphabetTable[A4[j]]);
      }
      while((i++ < 3)) {
        output[encodedLength++] = '=';
      }
    }
    output[encodedLength] = '\0';
    return encodedLength;
  }

  /// @brief Converts a base64 null-terminated string to an array of bytes.
  /// @param input Pointer to input string.
  /// @param output Pointer to output array.
  /// @param inputLength - Number of bytes to read from input pointer.
  /// @return Number of bytes in the decoded binary.
  static uint32_t decodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength) {
    int32_t i = 0, j = 0;
    uint32_t decodedLength = 0;
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
          output[decodedLength++] = A3[i];
        }
        i = 0;
      }
    }
    if(i) {
      for(j = i; j < 4; j++) {
        A4[j] = '\0';
      }
      for(j = 0; j < 4; j++) {
        A4[j] = lookupTable(A4[j]);
      }
      fromA4ToA3(A3, A4);
      for(j = 0; j < i - 1; j++) {
        output[decodedLength++] = A3[j];
      }
    }
    output[decodedLength] = '\0';
    return decodedLength;
  }

private:
  static inline void fromA3ToA4(uint8_t* A4, uint8_t* A3) {
    A4[0] = (A3[0] & 0xfc) >> 2;
    A4[1] = ((A3[0] & 0x03) << 4) + ((A3[1] & 0xf0) >> 4);
    A4[2] = ((A3[1] & 0x0f) << 2) + ((A3[2] & 0xc0) >> 6);
    A4[3] = (A3[2] & 0x3f);
  }

  static inline void fromA4ToA3(uint8_t* A3, uint8_t* A4) {
    A3[0] = (A4[0] << 2) + ((A4[1] & 0x30) >> 4);
    A3[1] = ((A4[1] & 0xf) << 4) + ((A4[2] & 0x3c) >> 2);
    A3[2] = ((A4[2] & 0x3) << 6) + A4[3];
  }

  static inline uint8_t lookupTable(char c) {
    if(c >='A' && c <='Z') return c - 'A';
    if(c >='a' && c <='z') return c - 71;
    if(c >='0' && c <='9') return c + 4;
    if(c == '+') return 62;
    if(c == '/') return 63;
    return -1;
  }

public:
  Base64(const Base64&) = delete;                       // Define copy constructor.
  Base64& operator=(const Base64&) = delete;            // Define copy assignment operator.
  Base64(Base64&&) = delete;                            // Define move constructor.
  Base64& operator=(Base64&&) = delete;                 // Define move assignment operator.

private:
  static const char PROGMEM _Base64AlphabetTable[];

};

const char Base64::_Base64AlphabetTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789+/";

#endif // BASE64_HPP