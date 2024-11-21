#ifndef COMMON_HPP
#define COMMON_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Utility class for time unit conversions and elapsed time checks.
class Time {
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

  /// @brief Checks if a specified duration has elapsed since an event.
  /// @param currentTime The current time (e.g., from a timer or clock).
  /// @param eventTimer The time of the event's start.
  /// @param duration The duration to check against.
  /// @return `true` if the duration has elapsed, `false` otherwise.
  static constexpr bool hasElapsed(uint32_t currentTime, uint32_t eventTimer, uint32_t duration) {
    return (currentTime - eventTimer) > duration;
  }
};
#endif // COMMON_HPP