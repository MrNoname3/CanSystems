#ifndef BASE64_HPP
#define BASE64_HPP

#include <Arduino.h>                          /// Arduino libraries header.

/// @brief Base64 encoding and decoding of strings. Uses '+' for 62, '/' for 63, '=' for padding.
class Base64 {
public:
  Base64() = delete;
  ~Base64() = delete;

private:
  /// @brief Converts a single byte from a binary value to the corresponding base64 character.
  /// @param v Byte to convert.
  /// @return Ascii code of base64 character. If byte is >= 64, then there is not corresponding base64 character and 255 is returned.
  static uint8_t binaryToBase64(uint8_t v) {
    if(v < 26) { return v + 'A'; }                  // Capital letters - 'A' is ascii 65 and base64 0.
    if(v < 52) { return v + 71; }                   // Lowercase letters - 'a' is ascii 97 and base64 26.
    if(v < 62) { return v - 4; }                    // Digits - '0' is ascii 48 and base64 52.
  #ifdef BASE64_URL
    if(v == 62) { return '-'; }                     // '-' is ascii 45 and base64 62.
    if(v == 63) { return '_'; }                     // '_' is ascii 95 and base64 62.
  #else
    if(v == 62) { return '+'; }                     // '+' is ascii 43 and base64 62.
    if(v == 63) { return '/'; }                     // '/' is ascii 47 and base64 63.
  #endif
    return 64;
  }

  /// @brief Converts a single byte from a base64 character to the corresponding binary value.
  /// @param c Base64 character (as ascii code).
  /// @return 6-bit binary value.
  static uint8_t base64ToBinary(uint8_t c) {
    if('A' <= c && c <= 'Z') { return c - 'A'; }    // Capital letters - 'A' is ascii 65 and base64 0.
    if('a' <= c && c <= 'z') { return c - 71; }     // Lowercase letters - 'a' is ascii 97 and base64 26.
    if('0' <= c && c <= '9') { return c + 4; }      // Digits - '0' is ascii 48 and base64 52.
  #ifdef BASE64_URL
    if(c == '-') { return 62; }                     // '-' is ascii 45 and base64 62.
    if(c == '_') { return 63; }                     // '_' is ascii 95 and base64 62
  #else
    if(c == '+') { return 62; }                     // '+' is ascii 43 and base64 62
    if(c == '/') { return 63; }                     // '/' is ascii 47 and base64 63
  #endif
    return 255;
  }

public:
  /// @brief Calculates length of base64 string needed for a given number of binary bytes.
  /// @param input_length Amount of binary data in bytes.
  /// @return Number of base64 characters needed to encode input_length bytes of binary data.
  static uint32_t encodeBase64Length(uint32_t input_length) {
    return (input_length + 2) / 3 * 4;
  }

  /// @brief Calculates number of bytes of binary data in a base64 string Variant that does not use input_length no longer used within library, retained for API compatibility.
  /// @param input Base64-encoded null-terminated string.
  /// @param input_length (optional) - Number of bytes to read from input pointer.
  /// @return Number of bytes of binary data in input.
  static uint32_t decodeBase64Length(const uint8_t input[], uint32_t input_length = -1) {
    const uint8_t* start = input;
    while(base64ToBinary(input[0]) < 64 && (uint32_t)(input - start) < input_length) {
      ++input;
    }
    input_length = (uint32_t)(input - start);
    return input_length / 4 * 3 + (input_length % 4 ? input_length % 4 - 1 : 0);
  }

  /// @brief Converts an array of bytes to a base64 null-terminated string.
  /// @param input Pointer to input data.
  /// @param output Pointer to output string. Null terminator will be added automatically.
  /// @param input_length Number of bytes to read from input pointer.
  /// @return Length of encoded string in bytes (not including null terminator).
  static uint32_t encodeBase64(const uint8_t input[], uint8_t output[], uint32_t input_length) {
    uint32_t full_sets = input_length / 3;

    // While there are still full sets of 24 bits...
    for(uint32_t i = 0; i < full_sets; ++i) {
      output[0] = binaryToBase64(                         input[0] >> 2);
      output[1] = binaryToBase64((input[0] & 0x03) << 4 | input[1] >> 4);
      output[2] = binaryToBase64((input[1] & 0x0F) << 2 | input[2] >> 6);
      output[3] = binaryToBase64( input[2] & 0x3F);
      input += 3;
      output += 4;
    }

    switch(input_length % 3) {
      case 0: {
        output[0] = '\0';
      } break;
      case 1: {
        output[0] = binaryToBase64(                         input[0] >> 2);
        output[1] = binaryToBase64((input[0] & 0x03) << 4);
        output[2] = '=';
        output[3] = '=';
        output[4] = '\0';
      } break;
      case 2: {
        output[0] = binaryToBase64(                         input[0] >> 2);
        output[1] = binaryToBase64((input[0] & 0x03) << 4 | input[1] >> 4);
        output[2] = binaryToBase64((input[1] & 0x0F) << 2);
        output[3] = '=';
        output[4] = '\0';
      } break;
    }
    return encodeBase64Length(input_length);
  }

  /// @brief Converts a base64 null-terminated string to an array of bytes.
  /// @param input Pointer to input string.
  /// @param output Pointer to output array.
  /// @param input_length (optional) - Number of bytes to read from input pointer.
  /// @return Number of bytes in the decoded binary.
  static uint32_t decodeBase64(const uint8_t input[], uint8_t output[], uint32_t input_length = -1) {
    uint32_t output_length = decodeBase64Length(input, input_length);

    // Decode each set of four Base64 characters to three bytes of binary data
    for (uint32_t i = 0; i < output_length; i += 3) {
      output[0] = (base64ToBinary(input[i]) << 2) | (base64ToBinary(input[i + 1]) >> 4);
      output[1] = (base64ToBinary(input[i + 1]) << 4) | (base64ToBinary(input[i + 2]) >> 2);
      output[2] = (base64ToBinary(input[i + 2]) << 6) | base64ToBinary(input[i + 3]);
      output += 3;
    }

    // Adjust for padding in the Base64 input
    switch (output_length % 3) {
      case 1: {
        output[0] = (base64ToBinary(input[output_length - 1]) << 2) | (base64ToBinary(input[output_length]) >> 4);
      } break;
      case 2: {
        output[0] = (base64ToBinary(input[output_length - 2]) << 2) | (base64ToBinary(input[output_length - 1]) >> 4);
        output[1] = (base64ToBinary(input[output_length - 1]) << 4) | (base64ToBinary(input[output_length]) >> 2);
      } break;
    }
    return output_length;
}

  Base64(const Base64&) = delete;                       // Define copy constructor.
  Base64& operator=(const Base64&) = delete;            // Define copy assignment operator.
  Base64(Base64&&) = delete;                            // Define move constructor.
  Base64& operator=(Base64&&) = delete;                 // Define move assignment operator.

};
#endif // BASE64_HPP