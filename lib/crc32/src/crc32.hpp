#ifndef CRC32_HPP
#define CRC32_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief A utility class for computing CRC32 checksums.
class Crc32 final {
private:
  static constexpr uint32_t defaultInitValue = 0xFFFFFFFF;          // Default initial value for CRC calculation.
  static constexpr uint32_t defaultPolynomial = 0xEDB88320;         // Default polynomial (IEEE CRC32 polynomial).

public:
  /// @brief Constructor to initialize CRC32 with a custom initial value and polynomial.
  /// @param initValue Initial value for the CRC computation. Default is 0xFFFFFFFF.
  /// @param polynomial Polynomial used in the CRC computation. Default is 0xEDB88320.
  explicit Crc32(uint32_t initValue = defaultInitValue, uint32_t polynomial = 0xEDB88320);

  /// @brief Default destructor.
  ~Crc32() = default;

  /// @brief Processes the next 8-bit value into the CRC computation.
  /// @param value The next byte of data to include in the checksum.
  void next(uint8_t value);

  /// @brief Processes an array of 8-bit values into the CRC computation.
  /// @param values Pointer to the array of data.
  /// @param length Number of bytes in the array.
  /// @note If the pointer is `nullptr` or `length` is 0, the function does nothing.
  void next(const uint8_t* values, uint32_t length);

  /// @brief Retrieves the current CRC32 checksum value.
  /// @return The current 32-bit CRC checksum, complemented for the final output.
  [[nodiscard]] uint32_t get() const;

  /// @brief Resets the CRC32 calculation to the initial value specified in the constructor.
  void reset();

  /// @brief Computes the CRC32 checksum for a given data array.
  /// @param data Pointer to the data array.
  /// @param length Number of bytes in the data array.
  /// @param initValue Initial CRC value to start the calculation. Default is 0xFFFFFFFF.
  /// @param polynomial Polynomial to use for the calculation. Default is 0xEDB88320.
  /// @return The computed CRC32 checksum.
  /// @note If the pointer is `nullptr` or `length` is 0, the function returns the initial value.
  static uint32_t calculate(const uint8_t *data, uint32_t length,
    uint32_t initValue = defaultInitValue, uint32_t polynomial = defaultPolynomial);

  /// @brief Verifies that a data array matches an expected CRC32 checksum.
  /// @param data Pointer to the data array.
  /// @param length Number of bytes in the data array.
  /// @param expected The expected CRC32 checksum.
  /// @param initValue Initial CRC value to start the calculation. Default is 0xFFFFFFFF.
  /// @param polynomial Polynomial to use for the calculation. Default is 0xEDB88320.
  /// @return `true` if the computed CRC matches the expected value; otherwise, `false`.
  static bool verify(const uint8_t* data, uint32_t length, uint32_t expected,
    uint32_t initValue = defaultInitValue, uint32_t polynomial = defaultPolynomial);

  Crc32(const Crc32&) = delete;                       // Define copy constructor.
  Crc32& operator=(const Crc32&) = delete;            // Define copy assignment operator.
  Crc32(Crc32&&) = delete;                            // Define move constructor.
  Crc32& operator=(Crc32&&) = delete;                 // Define move assignment operator.

private:
  uint32_t crc_;                                      // Current CRC value during computation.
  const uint32_t initValue_;                          // Initial value for the CRC computation.
  const uint32_t polynomial_;                         // Polynomial used for the CRC computation.
};
#endif // CRC32_HPP