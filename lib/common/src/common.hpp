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

/// @brief Provides common string constants for status messages and formatting.
class Str final {
public:
  /// @brief Default constructor.
  Str() = default;

  /// @brief Default destructor.
  ~Str() = default;

  /// @brief Retrieves the "OK" status string.
  /// @return Constant string `"[OK]"`.
  static constexpr const char* getOkStr() { return OK_STR; }

  /// @brief Retrieves the "Error" status string.
  /// @return Constant string `"[ERR]"`.
  static constexpr const char* getErrStr() { return ERR_STR; }

  /// @brief Retrieves the spacer string.
  /// @return Constant string `"|"`.
  static constexpr const char* getSpacerStr() { return SPACER_STR; }

  Str(const Str&) = delete;                       // Define copy constructor.
  Str& operator=(const Str&) = delete;            // Define copy assignment operator.
  Str(Str&&) = delete;                            // Define move constructor.
  Str& operator=(Str&&) = delete;                 // Define move assignment operator.

private:
  static constexpr const char* OK_STR           = "[OK]";
  static constexpr const char* ERR_STR          = "[ERR]";
  static constexpr const char* SPACER_STR       = "|";
};

/// @brief Class to provide build-time metadata and configuration information.
class Build final {
public:
  /// @brief Default constructor.
  Build() = default;

  /// @brief Default destructor.
  ~Build() = default;

  /// @brief Gets the firmware version.
  /// @return Firmware version derived from `GIT_COMMIT_COUNT`.
  static constexpr const uint16_t getFwVersion() { return fwVersion; }

  /// @brief Gets the Git commit hash.
  /// @return Git commit hash (`GIT_COMMIT_HASH`).
  static constexpr const uint32_t getGitHash() { return gitHash; }

  /// @brief Checks if the repository has uncommitted changes.
  /// @return `1` if dirty, `0` if clean (`GIT_DIRTY`).
  static constexpr const uint8_t getGitDirty() { return gitDirty; }

  /// @brief Gets the C++ standard version used for compilation.
  /// @return Value of the `__cplusplus` macro.
  static constexpr const uint32_t getCppVersion() { return cppVersion; }

  Build(const Build&) = delete;                       // Define copy constructor.
  Build& operator=(const Build&) = delete;            // Define copy assignment operator.
  Build(Build&&) = delete;                            // Define move constructor.
  Build& operator=(Build&&) = delete;                 // Define move assignment operator.

private:
  static constexpr uint16_t fwVersion = static_cast<uint16_t>(GIT_COMMIT_COUNT);      // Firmware version.
  static constexpr uint32_t gitHash = static_cast<uint32_t>(GIT_COMMIT_HASH);         // Git commit hash.
  static constexpr uint8_t gitDirty = static_cast<uint8_t>(GIT_DIRTY);                // Repository state.
  static constexpr uint32_t cppVersion = static_cast<uint32_t>(__cplusplus);          // C++ standard version.
};
#endif // COMMON_HPP