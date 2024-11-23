#ifndef COMMON_HPP
#define COMMON_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Utility class for time unit conversions and elapsed time checks.
class Time final {
public:
  /// @brief Default constructor.
  Time() = default;

  /// @brief Default destructor.
  ~Time() = default;

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

  Time(const Time&) = delete;                       // Define copy constructor.
  Time& operator=(const Time&) = delete;            // Define copy assignment operator.
  Time(Time&&) = delete;                            // Define move constructor.
  Time& operator=(Time&&) = delete;                 // Define move assignment operator.
};

/// @brief Utility class for configuring and managing analog input settings.
class Analog final {
public:
  /// @brief Default constructor.
  Analog() = default;

  /// @brief Default destructor.
  ~Analog() = default;

  /// @brief Configures the analog input settings.
  /// This function sets up the analog reference voltage to 5V and configures the ADC
  /// for fast sampling by setting the prescaler to 16.
  static void config() {
    analogReference(DEFAULT);
    bitSet(ADCSRA, ADPS2);
    bitSet(ADCSRA, ADPS1);
    bitClear(ADCSRA, ADPS0);
  }

  Analog(const Analog&) = delete;                       // Define copy constructor.
  Analog& operator=(const Analog&) = delete;            // Define copy assignment operator.
  Analog(Analog&&) = delete;                            // Define move constructor.
  Analog& operator=(Analog&&) = delete;                 // Define move assignment operator.
};
#endif // COMMON_HPP