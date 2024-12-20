#ifndef CONFIG_HANDLER_HPP
#define CONFIG_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief A utility class to manage configurations and error states.
class ConfigHandler final {
private:
  static constexpr uint8_t maxWifiSsidSize = 32U;                     // Maximum size of the Wi-Fi SSID string.
  static constexpr uint8_t maxWifiPasswordSize = 32U;                 // Maximum size of the Wi-Fi password string.

  /// @brief Delete constructor.
  ConfigHandler() = delete;

  /// @brief Delete destructor.
  ~ConfigHandler() = delete;

public:
  /// @brief Gets the maximum allowed size for the Wi-Fi SSID.
  /// @return Maximum size of the SSID string.
  static constexpr uint8_t getMaxWifiSsidSize() { return maxWifiSsidSize; }

  /// @brief Gets the maximum allowed size for the Wi-Fi password.
  /// @return Maximum size of the password string.
  static constexpr uint8_t getMaxWifiPasswordSize() { return maxWifiPasswordSize; }

  /// @brief Retrieves Wi-Fi configuration (SSID and password) from a file.
  /// @param ssid Buffer to store the retrieved SSID.
  /// @param password Buffer to store the retrieved password.
  /// @return Error state, where `0` means success.
  static uint16_t getWifiConfig(char (&ssid)[maxWifiSsidSize], char (&password)[maxWifiPasswordSize]);

  ConfigHandler(const ConfigHandler&) = delete;                       // Define copy constructor.
  ConfigHandler& operator=(const ConfigHandler&) = delete;            // Define copy assignment operator.
  ConfigHandler(ConfigHandler&&) = delete;                            // Define move constructor.
  ConfigHandler& operator=(ConfigHandler&&) = delete;                 // Define move assignment operator

private:
  /// @brief Enumeration representing possible error states.
  enum class ERROR : uint8_t {
    NONE                = 0U,                     // No error.
    NO_CONFIG_FILE      = 1 << 0U,                // Configuration file is missing.
    CANNOT_OPEN_FILE    = 1 << 1U,                // Unable to open the configuration file.
    JSON_PARSING_ERROR  = 1 << 2U,                // JSON parsing failed.
    MISSING_SSID_KEY    = 1 << 3U,                // SSID key is missing in the JSON.
    MISSING_PWD_KEY     = 1 << 4U,                // Password key is missing in the JSON.
    SSID_LENGTH_ERR     = 1 << 5U,                // SSID length is invalid.
    PWD_LENGTH_ERR      = 1 << 6U                 // Password length is invalid.
  };

  /// @brief Sets the current error state.
  /// @param err The error to set.
  static void setError(ERROR err);

  /// @brief Retrieves the current error state.
  /// @return The current error state.
  static uint8_t getError();

  /// @brief Clears the current error state.
  static void clearError();

  static uint8_t error;                           // Tracks the current error state as a bitmask.
};
#endif // CONFIG_HANDLER_HPP