#ifndef COMMON_HPP
#define COMMON_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Utility class for converting time units into milliseconds or other units.
/// @details Provides a collection of `constexpr` static methods to perform
/// time conversions at compile-time or runtime.
class TimeConverter {
public:
  /// @brief Converts hours to milliseconds.
  /// @param hour The number of hours to convert.
  /// @return The equivalent time in milliseconds.
  static constexpr uint32_t hrToMs(uint16_t hour) {
    return hour * 60UL * 60UL * 1000UL;
  }

  /// @brief Converts minutes to milliseconds.
  /// @param minute The number of minutes to convert.
  /// @return The equivalent time in milliseconds.
  static constexpr uint32_t minToMs(uint16_t minute) {
    return minute * 60UL * 1000UL;
  }

  /// @brief Converts seconds to milliseconds.
  /// @param second The number of seconds to convert.
  /// @return The equivalent time in milliseconds.
  static constexpr uint32_t secToMs(uint16_t second) {
    return second * 1000UL;
  }

  /// @brief Converts hours to minutes.
  /// @param hour The number of hours to convert.
  /// @return The equivalent time in minutes.
  static constexpr uint16_t hrToMin(uint16_t hour) {
    return hour * 60U;
  }
};
#endif // COMMON_HPP