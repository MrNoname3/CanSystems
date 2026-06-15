#ifndef CRC16_HPP
#define CRC16_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief A utility class for computing CRC16 checksums.
class Crc16 final {
private:
  static constexpr uint16_t defaultInitValue = 0U;                  // Default initial value for CRC calculation.
  static constexpr uint16_t defaultPolynomial = 0x1021U;            // Default polynomial (CRC-CCITT).

public:
  /// @brief Constructor to initialize CRC16 with a custom initial value and polynomial.
  /// @param initValue Initial value for CRC computation. Default is 0.
  /// @param polynomial Polynomial to use for the CRC computation. Default is 0x1021 (CRC-CCITT).
  explicit Crc16(uint16_t initValue = defaultInitValue, uint16_t polynomial = defaultPolynomial);

  /// @brief Default destructor.
  ~Crc16() = default;

  /// @brief Processes the next 8-bit value into the CRC computation.
  /// @param value The next byte of data to include in the checksum.
  void next(uint8_t value);

  /// @brief Processes an array of 8-bit values into the CRC computation.
  /// @param values Pointer to the array of data.
  /// @param length Number of bytes in the array.
  /// @note If the pointer is `nullptr` or `length` is 0, the function does nothing.
  void next(const uint8_t* values, uint32_t length);

  /// @brief Retrieves the current CRC16 checksum value.
  /// @return The current 16-bit CRC checksum.
  [[nodiscard]] uint16_t get() const;

  /// @brief Resets the CRC16 calculation to the initial value specified in the constructor.
  void reset();

  /// @brief Computes the CRC16 checksum for a given data array.
  /// @param data Pointer to the data array.
  /// @param length Number of bytes in the data array.
  /// @param initValue Initial CRC value to start the calculation. Default is 0.
  /// @param polynomial Polynomial to use for the calculation. Default is 0x1021.
  /// @return The computed CRC16 checksum.
  /// @note If the pointer is `nullptr` or `length` is 0, the function returns the initial value.
  [[nodiscard]] static uint16_t calculate(const uint8_t* data, uint32_t length, uint16_t initValue = defaultInitValue, uint16_t polynomial = defaultPolynomial);

  /// @brief Verifies that a data array matches an expected CRC16 checksum.
  /// @param data Pointer to the data array.
  /// @param length Number of bytes in the data array.
  /// @param expected The expected CRC16 checksum.
  /// @param initValue Initial CRC value to start the calculation. Default is 0.
  /// @param polynomial Polynomial to use for the calculation. Default is 0x1021.
  /// @return `true` if the computed CRC matches the expected value; otherwise, `false`.
  [[nodiscard]] static bool verify(const uint8_t* data, uint32_t length, uint16_t expected, uint16_t initValue = defaultInitValue, uint16_t polynomial = defaultPolynomial);

  Crc16(const Crc16&) = delete;                       // Define copy constructor.
  Crc16& operator=(const Crc16&) = delete;            // Define copy assignment operator.
  Crc16(Crc16&&) = delete;                            // Define move constructor.
  Crc16& operator=(Crc16&&) = delete;                 // Define move assignment operator.

private:
  uint16_t crc_;                                      // Current CRC value during computation.
  const uint16_t initValue_;                          // Initial value for the CRC computation.
  const uint16_t polynomial_;                         // Polynomial used for the CRC computation.
};
#endif // CRC16_HPP
