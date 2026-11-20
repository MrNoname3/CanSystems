#pragma once

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief The 16-bit value two bytes encode, least significant byte first.
/// @details The high byte is widened to unsigned int before the shift: a byte promotes to int, and
/// unsigned int is at least 16 bits wide on every target while never promoting further.
constexpr uint16_t le16(uint8_t low, uint8_t high) {
  return static_cast<uint16_t>((static_cast<unsigned int>(high) << 8U) | low);
}

/// @brief The 16-bit value two bytes encode, most significant byte first.
constexpr uint16_t be16(uint8_t high, uint8_t low) { return le16(low, high); }
