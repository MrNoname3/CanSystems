#pragma once
#include <Arduino.h>                                                /// Arduino libraries header.
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <HardwareSerial.h>                                         /// Hardware serial driver for communication with peripheral devices.
#if defined(ESP8266) || defined(ESP32)
#include <pgmspace.h>                                               /// Provides PROGMEM support for storing data in flash memory.
#endif

/// @brief Utility class for time unit conversions and elapsed time checks.
class Time final {
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

  /// @brief Converts milliseconds to microseconds.
  /// @param ms The number of milliseconds to convert.
  /// @return The equivalent time in microseconds.
  static constexpr uint32_t msToUs(uint16_t ms) {
    return ms * 1000UL;
  }

  /// @brief Checks if a specified duration has elapsed since an event.
  /// @param currentTime The current time (e.g., from a timer or clock).
  /// @param eventTimer The time of the event's start.
  /// @param duration The duration to check against.
  /// @return `true` if the duration has elapsed, `false` otherwise.
  static constexpr bool hasElapsed(uint32_t currentTime, uint32_t eventTimer, uint32_t duration) {
    return (currentTime - eventTimer) > duration;
  }

  Time() = delete;                                   // Delete constructor.
  ~Time() = delete;                                  // Delete destructor.
  Time(const Time&) = delete;                       // Define copy constructor.
  Time& operator=(const Time&) = delete;            // Define copy assignment operator.
  Time(Time&&) = delete;                            // Define move constructor.
  Time& operator=(Time&&) = delete;                 // Define move assignment operator.
};

/// @brief Utility class for configuring and managing analog input settings.
class Analog final {
public:
#if defined(__AVR_ATmega328P__)
  /// @brief Configures the analog input settings.
  /// This function sets up the analog reference voltage to 5V and configures the ADC
  /// for fast sampling by setting the prescaler to 16.
  static void config() {
    analogReference(DEFAULT);
    bitSet(ADCSRA, ADPS2);
    bitSet(ADCSRA, ADPS1);
    bitClear(ADCSRA, ADPS0);
  }
#endif

  /// @brief Applies a complementary filter to smooth sensor values.
  /// @tparam alpha Weight for the new value (0-255).
  /// @param newValue The latest sensor value.
  /// @param oldValue The previous smoothed value.
  /// @return The updated smoothed value.
  template <uint8_t alpha>
  static constexpr uint16_t complementaryFilter(uint16_t newValue, uint16_t oldValue) {
    return static_cast<uint16_t>((static_cast<uint32_t>(alpha) * newValue + static_cast<uint32_t>(255U - alpha) * oldValue) / 255U);
  }

  /// @brief Applies a complementary filter with a fixed alpha of ~10%.
  /// @param newValue The latest sensor value.
  /// @param oldValue The previous smoothed value.
  /// @return The updated smoothed value.
  static constexpr uint16_t complementaryFilter10(uint16_t newValue, uint16_t oldValue) {
    return complementaryFilter<25U>(newValue, oldValue);  // Alpha ~10% (25/255).
  }

  Analog() = delete;                                   // Delete constructor.
  ~Analog() = delete;                                  // Delete destructor.
  Analog(const Analog&) = delete;                       // Define copy constructor.
  Analog& operator=(const Analog&) = delete;            // Define copy assignment operator.
  Analog(Analog&&) = delete;                            // Define move constructor.
  Analog& operator=(Analog&&) = delete;                 // Define move assignment operator.
};

/// @brief Provides common string constants for status messages and formatting.
class Str final {
public:
  /// @brief Retrieves the "OK" status string.
  /// @return Constant string `"[OK]"`.
  static constexpr const char* getOkStr() { return okStr; }

  /// @brief Retrieves the "Error" status string.
  /// @return Constant string `"[ERR]"`.
  static constexpr const char* getErrStr() { return errStr; }

#if defined(__AVR_ATmega328P__)
  /// @brief Retrieves the spacer string.
  /// @return Constant string `"|"`.
  static constexpr const char* getSpacerStr() { return spacerStr; }
#endif

  /// @brief Retrieves a status string based on a boolean state.
  /// @param state Boolean value where `true` represents "OK" and `false` represents "Error".
  /// @return `"[OK]"` if `state` is `true`, otherwise `"[ERR]"`.
  static constexpr const char* getStateStr(bool state) { return state ? getOkStr() : getErrStr(); }

#if defined(ESP8266) || defined(ESP32)
  /// @brief Retrieves the section separator string.
  /// @return Constant string used to separate sections in logs or messages, stored in program memory.
  static constexpr const char* getSectionSeparator() { return sectionSeparator; }
#endif

  Str() = delete;                                   // Delete constructor.
  ~Str() = delete;                                  // Delete destructor.
  Str(const Str&) = delete;                       // Define copy constructor.
  Str& operator=(const Str&) = delete;            // Define copy assignment operator.
  Str(Str&&) = delete;                            // Define move constructor.
  Str& operator=(Str&&) = delete;                 // Define move assignment operator.

private:
#if defined(__AVR_ATmega328P__)
  static constexpr const char* okStr            = "[OK]";     // Status string for "OK" on AVR platforms.
  static constexpr const char* errStr           = "[ERR]";    // Status string for "Error" on AVR platforms.
  static constexpr const char* spacerStr        = "|";        // A string used as a spacer in formatting.
#elif defined(ESP8266) || defined(ESP32)
  static constexpr const char PROGMEM okStr[]      = "[OK]";  // Status string for "OK" stored in program memory for ESP platforms.
  static constexpr const char PROGMEM errStr[]     = "[ERR]"; // Status string for "Error" stored in program memory for ESP platforms.
  static constexpr const char PROGMEM sectionSeparator[] = {  // Section separator string, stored in program memory on ESP platforms.
    "*************************************************"
  };
#endif
};

/// @brief Class to provide build-time metadata and configuration information.
class Build final {
public:
  /// @brief Gets the firmware version.
  /// @return Firmware version derived from `GIT_COMMIT_COUNT`.
  static constexpr uint16_t getFwVersion() { return fwVersion; }

  /// @brief Gets the Git commit hash.
  /// @return Git commit hash (`GIT_COMMIT_HASH`).
  static constexpr uint32_t getGitHash() { return gitHash; }

  /// @brief Checks if the repository has uncommitted changes.
  /// @return `1` if dirty, `0` if clean (`GIT_DIRTY`).
  static constexpr uint8_t getGitDirty() { return gitDirty; }

  /// @brief Gets the C++ standard version used for compilation.
  /// @return Value of the `__cplusplus` macro.
  static constexpr uint32_t getCppVersion() { return cppVersion; }

  /// @brief Gets the PlatformIO environment name used during the build.
  /// @return A constant string containing the PlatformIO environment name (`BUILD_ENV_NAME`).
  static constexpr const char* getPioEnv() { return pioEnv; }

  /// @brief Gets the length of the PlatformIO environment name string.
  /// @return Length of the `pioEnv` string excluding the null terminator.
  static constexpr uint32_t getPioEnvLength() { return sizeof(pioEnv) - 1U; }

  /// @brief Retrieves the PlatformIO environment information in JSON format.
  /// @return A constant string in JSON format with the environment name.
  static constexpr const char* getPioEnvJson() { return pioEnvJson; }

  /// @brief Retrieves the length of the JSON-formatted PlatformIO environment information.
  /// @return Length of the `pioEnvJson` string excluding the null terminator.
  static constexpr uint32_t getPioEnvJsonLength() { return sizeof(pioEnvJson) - 1U; }

  /// @brief Prints build metadata and configuration to a serial interface.
  static void printBuildInfo();

  Build() = delete;                                   // Delete constructor.
  ~Build() = delete;                                  // Delete destructor.
  Build(const Build&) = delete;                       // Define copy constructor.
  Build& operator=(const Build&) = delete;            // Define copy assignment operator.
  Build(Build&&) = delete;                            // Define move constructor.
  Build& operator=(Build&&) = delete;                 // Define move assignment operator.

private:
  static constexpr uint16_t fwVersion = static_cast<uint16_t>(GIT_COMMIT_COUNT);      // Firmware version derived from commit count.
  static constexpr uint32_t gitHash = static_cast<uint32_t>(GIT_COMMIT_HASH);         // Git commit hash of the build.
  static constexpr uint8_t gitDirty = static_cast<uint8_t>(GIT_DIRTY);                // Repository state indicating uncommitted changes.
  static constexpr uint32_t cppVersion = static_cast<uint32_t>(__cplusplus);          // C++ standard version used for compilation.
  static constexpr const char pioEnv[] = BUILD_ENV_NAME;                              // Name of the PlatformIO environment used for the build.
  static constexpr const char pioEnvJson[] = "{\"Env\":\"" BUILD_ENV_NAME "\"}";      // JSON-formatted string containing the PlatformIO environment name.
};

/// @brief Utility class for managing predefined file names in the file system.
class FileName final {
public:
#if defined(ESP8266) || defined(ESP32)
  /// @brief Retrieves the temporary file location.
  /// @return Constant string representing the path to the temporary file.
  static constexpr const char* getTempFileLocation() { return tempFileLocation; }

  /// @brief Retrieves the OTA firmware file location.
  /// @return Constant string representing the path to the OTA firmware file.
  static constexpr const char* getOtaFwLocation() { return otaFwLocation; }

  /// @brief Retrieves the external device OTA firmware file location.
  /// @return Constant string representing the path to the external device OTA firmware file.
  static constexpr const char* getExtOtaFwLocation() { return extOtaFwLocation; }

  /// @brief Retrieves the Wi-Fi configuration file location.
  /// @return Constant string representing the path to the Wi-Fi configuration file.
  static constexpr const char* getWifiConfigLocation() { return getMqttServerCredentialsLocation(); }

  /// @brief Retrieves the MQTT server certificate file location.
  /// @return Constant string representing the path to the MQTT server certificate file.
  static constexpr const char* getMqttServerCertLocation() { return mqttServerCertLocation; }

  /// @brief Retrieces the MQTT server credentials file location.
  /// @return Constant string representing the path to the MQTT server credentials file.
  static constexpr const char* getMqttServerCredentialsLocation() { return mqttServerCredLocation; }
#endif

  FileName() = delete;                                   // Delete constructor.
  ~FileName() = delete;                                  // Delete destructor.
  FileName(const FileName&) = delete;                       // Define copy constructor.
  FileName& operator=(const FileName&) = delete;            // Define copy assignment operator.
  FileName(FileName&&) = delete;                            // Define move constructor.
  FileName& operator=(FileName&&) = delete;                 // Define move assignment operator.

private:
#if defined(ESP8266) || defined(ESP32)
  static constexpr const char PROGMEM tempFileLocation[]       = "/temp.tmp";             // Temporary file name used during file transfer.
  static constexpr const char PROGMEM otaFwLocation[]          = "espFirmware";            // File location for the OTA firmware.
  static constexpr const char PROGMEM extOtaFwLocation[]       = "/%sFirmware.bin";       // File location for external device OTA firmware.
//  static constexpr const char PROGMEM wifiConfigLocation[]     = "/config/wifi.json";     // File location for the Wi-Fi configuration.
  static constexpr const char PROGMEM mqttServerCertLocation[] = "/config/mosq-ca.crt";   // File location for the MQTT server certificate.
  static constexpr const char PROGMEM mqttServerCredLocation[] = "/config/server.json";   // File location for the MQTT server credentials.
#endif
};

/// @brief Template class to manage error states using a bitmask.
/// @tparam Enum An enumeration type representing individual error states.
/// @tparam StorageType An integral type used to store the bitmask. Must be large enough to hold all Enum values.
template <typename Enum, typename StorageType>
class ErrorState final {
  // Ensure that Enum is an enumeration type and StorageType is large enough to hold all Enum values.
  static_assert(sizeof(StorageType) * 8U >= sizeof(Enum) * 8U, "StorageType must be large enough to hold all Enum values!");
public:
  /// @brief Constructs an `ErrorState` object with all error states cleared.
  ErrorState() :
    errorState(0U)
  {}

  /// @brief Default destructor.
  ~ErrorState() = default;

  /// @brief Sets a specific error state.
  /// @param error The error to set.
  void setError(Enum error) {
    errorState |= static_cast<StorageType>(error);
  }

  /// @brief Checks if a specific error state is set.
  /// @param error The error to check.
  /// @return `true` if the error is set, otherwise `false`.
  bool hasError(Enum error) const {
    return (errorState & static_cast<StorageType>(error)) != 0U;
  }

  /// @brief Clears a specific error state.
  /// @param error The error to clear.
  void clearError(Enum error) {
    errorState &= ~static_cast<StorageType>(error);
  }

  /// @brief Clears all error states.
  void clearAllErrors() {
    errorState = 0U;
  }

  /// @brief Retrieves the raw bitmask representing all error states.
  /// @return The raw error state as a value of type `StorageType`.
  StorageType getRawErrorState() const {
    return errorState;
  }

  /// @brief Checks if any error state is set.
  /// @return `true` if any error is set, otherwise `false`.
  [[nodiscard]] bool hasAnyError() const {
    return errorState != 0U;
  }

  ErrorState(const ErrorState&) = delete;                       // Define copy constructor.
  ErrorState& operator=(const ErrorState&) = delete;            // Define copy assignment operator.
  ErrorState(ErrorState&&) = delete;                            // Define move constructor.
  ErrorState& operator=(ErrorState&&) = delete;                 // Define move assignment operator.

private:
  StorageType errorState;                           // Stores the current error state as a bitmask.
};

/// @brief A static logger for logging via a hardware serial interface.
class Logger final {
private:
  /// @brief Default constructor. Prevents instantiation of the class.
  Logger() = default;

  /// @brief Default destructor.
  ~Logger() = default;

public:
  /// @brief Initialize the logger with a hardware serial instance.
  /// @param serialPort The hardware serial instance (e.g., `Serial`, `Serial1`).
  static inline void begin(HardwareSerial &serialPort) noexcept {
    serial = &serialPort;
  }

  /// @brief Get the current hardware serial instance.
  /// @return Reference to the hardware serial instance.
  static inline HardwareSerial &get() noexcept {
    return *serial;
  }

  Logger(const Logger&) = delete;                       // Define copy constructor.
  Logger& operator=(const Logger&) = delete;            // Define copy assignment operator.
  Logger(Logger&&) = delete;                            // Define move constructor.
  Logger& operator=(Logger&&) = delete;                 // Define move assignment operator.

private:
  static inline HardwareSerial *serial = &Serial;   // Pointer to the hardware serial instance, defaults to `Serial`.
};

/// @brief Returns the number of elements in a fixed-size array.
/// @tparam T Element type of the array.
/// @tparam N Number of elements in the array.
/// @param arr Reference to the fixed-size array.
/// @return Number of elements as a compile-time constant.
template <typename T, uint8_t N>
constexpr uint8_t arraySize(T (&arr)[N]) { (void)arr; return N; }