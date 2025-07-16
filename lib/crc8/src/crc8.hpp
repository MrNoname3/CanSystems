#ifndef CRC8_HPP
#define CRC8_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief A utility class for computing CRC8 checksums.
class Crc8 final {
private:
  static constexpr uint8_t defaultInitValue = 0U;                   // Default initial value for CRC calculation.
  static constexpr uint8_t defaultPolynomial = 0x07U;               // Default polynomial (CRC-8-SMBus).

public:
  /// @brief Constructor to initialize CRC8 with a custom initial value and polynomial.
  /// @param initValue Initial value for CRC computation. Default is 0.
  /// @param polynomial Polynomial to use for the CRC computation. Default is 0x07 (CRC-8-SMBus).
  /// @param refIn Reflect input bytes. Default is false.
  /// @param refOut Reflect output result. Default is false.
  /// @param xorOut XOR value applied to final result. Default is 0.
  explicit Crc8(uint8_t initValue = defaultInitValue, uint8_t polynomial = defaultPolynomial, 
                bool refIn = false, bool refOut = false, uint8_t xorOut = 0);

  /// @brief Default destructor.
  ~Crc8() = default;

  /// @brief Processes the next 8-bit value into the CRC computation.
  /// @param value The next byte of data to include in the checksum.
  void next(uint8_t value);

  /// @brief Processes an array of 8-bit values into the CRC computation.
  /// @param values Pointer to the array of data.
  /// @param length Number of bytes in the array.
  /// @note If the pointer is `nullptr` or `length` is 0, the function does nothing.
  void next(const uint8_t* values, uint32_t length);

  /// @brief Retrieves the current CRC8 checksum value.
  /// @return The current 8-bit CRC checksum.
  uint8_t get() const;

  /// @brief Resets the CRC8 calculation to the initial value specified in the constructor.
  void reset();

  /// @brief Computes the CRC8 checksum for a given data array.
  /// @param data Pointer to the data array.
  /// @param length Number of bytes in the data array.
  /// @param initValue Initial CRC value to start the calculation. Default is 0.
  /// @param polynomial Polynomial to use for the calculation. Default is 0x07.
  /// @param refIn Reflect input bytes. Default is false.
  /// @param refOut Reflect output result. Default is false.
  /// @param xorOut XOR value applied to final result. Default is 0.
  /// @return The computed CRC8 checksum.
  /// @note If the pointer is `nullptr` or `length` is 0, the function returns the initial value.
  static uint8_t calculate(const uint8_t *data, uint32_t length,
    uint8_t initValue = defaultInitValue, uint8_t polynomial = defaultPolynomial,
    bool refIn = false, bool refOut = false, uint8_t xorOut = 0);

  /// @brief Verifies that a data array matches an expected CRC8 checksum.
  /// @param data Pointer to the data array.
  /// @param length Number of bytes in the data array.
  /// @param expected The expected CRC8 checksum.
  /// @param initValue Initial CRC value to start the calculation. Default is 0.
  /// @param polynomial Polynomial to use for the calculation. Default is 0x07.
  /// @param refIn Reflect input bytes. Default is false.
  /// @param refOut Reflect output result. Default is false.
  /// @param xorOut XOR value applied to final result. Default is 0.
  /// @return `true` if the computed CRC matches the expected value; otherwise, `false`.
  static bool verify(const uint8_t* data, uint32_t length, uint8_t expected,
    uint8_t initValue = defaultInitValue, uint8_t polynomial = defaultPolynomial,
    bool refIn = false, bool refOut = false, uint8_t xorOut = 0);

  Crc8(const Crc8&) = delete;                        // Define copy constructor.
  Crc8& operator=(const Crc8&) = delete;             // Define copy assignment operator.
  Crc8(Crc8&&) = delete;                             // Define move constructor.
  Crc8& operator=(Crc8&&) = delete;                  // Define move assignment operator.

private:
  uint8_t crc_;                                       // Current CRC value during computation.
  const uint8_t initValue_;                           // Initial value for the CRC computation.
  const uint8_t polynomial_;                          // Polynomial used for the CRC computation.
  const bool refIn_;                                  // Reflect input bytes.
  const bool refOut_;                                 // Reflect output result.
  const uint8_t xorOut_;                              // XOR value applied to final result.

  /// @brief Helper function to reflect (reverse) the bits of a byte.
  /// @param value The byte value to reflect.
  /// @return The reflected byte value.
  static uint8_t reflect(uint8_t value);
};
#endif // CRC8_HPP