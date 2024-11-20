#ifndef CRC16_HPP
#define CRC16_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief A utility class for calculating CRC16 checksum values.
class Crc16 final {
public:
  /// @brief Constructor to initialize CRC16 with a custom initial value and polynomial.
  /// @param initValue Initial value for the CRC calculation. Default is 0.
  /// @param polynomial Polynomial used in the CRC calculation. Default is 0x1021.
  explicit Crc16(uint16_t initValue = 0U, uint16_t polynomial = 0x1021U);

  /// @brief Default destructor.
  ~Crc16() = default;

  /// @brief Updates the CRC with the next 8-bit value.
  /// @param value 8-bit value to process.
  void next(uint8_t value);

  /// @brief Updates the CRC with an array of 8-bit values.
  /// @param values Pointer to the array of 8-bit values.
  /// @param length Length of the array.
  void next(const uint8_t* values, uint32_t length);

  /// @brief Retrieves the current CRC16 checksum value.
  /// @return The current 16-bit CRC value.
  uint16_t get() const;

  /// @brief Resets the CRC16 value to the initial value specified in the constructor.
  void reset();

  /// @brief Static method to calculate CRC16 for a given array of data.
  /// @param data Pointer to the array of 8-bit values.
  /// @param length Length of the array.
  /// @return The calculated CRC16 value.
  static uint16_t calculate(const uint8_t *data, uint32_t length);

  Crc16(const Crc16&) = delete;                       // Define copy constructor.
  Crc16& operator=(const Crc16&) = delete;            // Define copy assignment operator.
  Crc16(Crc16&&) = delete;                            // Define move constructor.
  Crc16& operator=(Crc16&&) = delete;                 // Define move assignment operator.

private:
  uint16_t crc_;                                      // Current CRC16 value.
  const uint16_t initValue_;                          // Initial CRC value used in reset.
  const uint16_t polynomial_;                         // Polynomial used in CRC calculation.
};
#endif // CRC16_HPP