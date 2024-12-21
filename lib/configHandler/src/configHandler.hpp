#ifndef CONFIG_HANDLER_HPP
#define CONFIG_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "common.hpp"                                               /// Common definitions and functions.
#include <functional>                                               /// For callback functions.

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
  static uint8_t getWifiConfig(char (&ssid)[maxWifiSsidSize], char (&password)[maxWifiPasswordSize]);

  /// @brief Retrieves the server certificate using a callback for storage.
  /// @param storeCert A callback function to handle the storage of the certificate.
  /// The callback receives a reference to a `Stream` and the certificate size in bytes.
  /// @return Error state as a `uint8_t`, where `0` means success.
  static uint8_t getServerCert(std::function<bool(Stream&, size_t)> storeCert);

  ConfigHandler(const ConfigHandler&) = delete;                       // Define copy constructor.
  ConfigHandler& operator=(const ConfigHandler&) = delete;            // Define copy assignment operator.
  ConfigHandler(ConfigHandler&&) = delete;                            // Define move constructor.
  ConfigHandler& operator=(ConfigHandler&&) = delete;                 // Define move assignment operator

private:
  /// @brief Enumeration representing possible error states for Wi-Fi configuration.
  enum class WifiConfigError : uint8_t {
    NONE                = 0U,                     // No error.
    NO_WIFI_CONFIG_FILE = 1 << 0U,                // Wi-Fi configuration file is missing.
    CANNOT_OPEN_FILE    = 1 << 1U,                // Unable to open the configuration file.
    JSON_PARSING_ERROR  = 1 << 2U,                // JSON parsing failed.
    MISSING_SSID_KEY    = 1 << 3U,                // SSID key is missing in the JSON.
    MISSING_PWD_KEY     = 1 << 4U,                // Password key is missing in the JSON.
    SSID_LENGTH_ERR     = 1 << 5U,                // SSID length is invalid.
    PWD_LENGTH_ERR      = 1 << 6U                 // Password length is invalid.
  };

  /// @brief Enumeration representing possible error states for server certificate retrieval.
  enum class ServerCertError : uint8_t {
    NONE                = 0U,                     // No error.
    NO_SERVER_CERT_FILE = 1 << 0U,                // Server certification file is missing.
    CANNOT_OPEN_FILE    = 1 << 1U,                // Unable to open the configuration file.
    CERT_FILE_EMPTY     = 1 << 2U,                // Server certificate file is empty.
    CALLBACK_NULLPTR    = 1 << 3U,                // Callback function is nullptr.
    CERT_STORING_FAILED = 1 << 4U                 // Unable to store the certificate.
  };

  static ErrorState<WifiConfigError, uint8_t> wifiConfErrState;       // Manages error states for Wi-Fi configuration.
  static ErrorState<ServerCertError, uint8_t> serverCertErrState;     // Manages error states for server certificate retrieval.
};
#endif // CONFIG_HANDLER_HPP