#ifndef BASE64_HPP
#define BASE64_HPP

#include <stdint.h>
#if (defined(__AVR__) || defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_SAM))
#include <avr/pgmspace.h>
#else
#include <pgmspace.h>
#endif

/// @brief Base64 encoding and decoding of strings. Uses '+' for 62, '/' for 63, '=' for padding.
class Base64 final {
public:
  Base64() = delete;
  ~Base64() = delete;

public:
  /// @brief Calculates length of base64 string needed for a given number of binary bytes.
  /// @param plainLength Amount of binary data in bytes.
  /// @return Number of base64 characters needed to encode input_length bytes of binary data.
  static uint32_t encodedLength(uint32_t plainLength);

  /// @brief Calculates number of bytes of binary data in a base64 string.
  /// @param input Base64-encoded null-terminated string.
  /// @param inputLength Number of bytes to read from input pointer.
  /// @return Number of bytes of binary data in input.
  static uint32_t decodedLength(const uint8_t input[], uint32_t inputLength);

  /// @brief Converts an array of bytes to a base64 null-terminated string.
  /// @param input Pointer to input data.
  /// @param output Pointer to output string. Null terminator will be added automatically.
  /// @param inputLength Number of bytes to read from input pointer.
  /// @return Length of encoded string in bytes (not including null terminator).
  static uint32_t encodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength);

  /// @brief Converts a base64 null-terminated string to an array of bytes.
  /// @param input Pointer to input string.
  /// @param output Pointer to output array.
  /// @param inputLength - Number of bytes to read from input pointer.
  /// @return Number of bytes in the decoded binary.
  static uint32_t decodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength);

private:
  static inline void fromA3ToA4(uint8_t* A4, const uint8_t* A3);
  static inline void fromA4ToA3(uint8_t* A3, const uint8_t* A4);
  static inline uint8_t lookupTable(char c);

public:
  Base64(const Base64&) = delete;                       // Define copy constructor.
  Base64& operator=(const Base64&) = delete;            // Define copy assignment operator.
  Base64(Base64&&) = delete;                            // Define move constructor.
  Base64& operator=(Base64&&) = delete;                 // Define move assignment operator.

private:
  static const char PROGMEM base64AlphabetTable_[];
};
#endif // BASE64_HPP