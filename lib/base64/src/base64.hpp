#ifndef BASE64_HPP
#define BASE64_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#if (defined(__AVR__) || defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_SAM))
#include <avr/pgmspace.h>                                           /// PROGMEM compatibility for AVR architectures.
#else
#include <pgmspace.h>                                               /// PROGMEM compatibility for other architectures.
#endif

/// @brief Provides static methods for Base64 encoding and decoding of binary data.
/// @details This class supports converting between binary data and its Base64 representation.
/// It uses '+' (for 62) and '/' (for 63) as special characters for Base64 encoding and '=' for padding.
class Base64 final {
private:
  // Static array containing the Base64 alphabet table in PROGMEM.
  static constexpr const char PROGMEM base64AlphabetTable[] = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/"
  };

public:
  /// @brief Calculates the length of a Base64-encoded string for a given number of bytes of input data.
  /// @param plainLength Number of bytes in the input binary data.
  /// @return Number of characters required for the Base64-encoded string (excluding null terminator).
  static uint32_t encodedLength(uint32_t plainLength);

  /// @brief Calculates the length of the decoded binary data from a Base64-encoded string.
  /// @param input Pointer to the Base64-encoded null-terminated input data.
  /// @param inputLength Number of bytes in the input Base64 string.
  /// @return Number of bytes in the decoded binary data.
  static uint32_t decodedLength(const uint8_t input[], uint32_t inputLength);

  /// @brief Encodes binary data into a Base64-encoded null-terminated string.
  /// @param input Pointer to the binary input data.
  /// @param output Pointer to the buffer where the Base64-encoded string will be written.
  /// @param inputLength Number of bytes in the input binary data.
  /// @param outputLength Size of the output buffer in bytes.
  /// @return Length of the encoded Base64 string (excluding null terminator), or 0 on error.
  static uint32_t encodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength, uint32_t outputLength);

  /// @brief Decodes a Base64-encoded null-terminated string into binary data.
  /// @param input Pointer to the Base64-encoded input string.
  /// @param output Pointer to the buffer where the decoded binary data will be written.
  /// @param inputLength Number of bytes in the input Base64 string.
  /// @param outputLength Size of the output buffer in bytes.
  /// @return Number of bytes written to the decoded binary output, or 0 on error.
  static uint32_t decodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength, uint32_t outputLength);

  Base64() = delete;                                   // Delete constructor.
  ~Base64() = delete;                                  // Delete destructor.
  Base64(const Base64&) = delete;                       // Define copy constructor.
  Base64& operator=(const Base64&) = delete;            // Define copy assignment operator.
  Base64(Base64&&) = delete;                            // Define move constructor.
  Base64& operator=(Base64&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Converts three bytes of binary data into four Base64 indices.
  /// @param A4 Pointer to the output array of 4 Base64 indices.
  /// @param A3 Pointer to the input array of 3 binary bytes.
  static void fromA3ToA4(uint8_t* A4, const uint8_t* A3);

  /// @brief Converts four Base64 indices into three bytes of binary data.
  /// @param A3 Pointer to the output array of 3 binary bytes.
  /// @param A4 Pointer to the input array of 4 Base64 indices.
  static void fromA4ToA3(uint8_t* A3, const uint8_t* A4);

  /// @brief Looks up the Base64 index corresponding to a given character.
  /// @param c The Base64 character to convert.
  /// @return The Base64 index (0-63) corresponding to the input character.
  /// @note Returns -1 for invalid characters.
  static uint8_t lookupTable(char c);

  static bool processFullBlock(uint8_t (&A4)[4], uint8_t output[], uint32_t &decodedLength);
  static bool processPartialBlock(uint8_t (&A4)[4], uint32_t count, uint8_t output[], uint32_t &decodedLength);
};
#endif // BASE64_HPP