#ifndef CONFIG_HANDLER_HPP
#define CONFIG_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "common.hpp"                                               /// Common definitions and functions.
#include <functional>                                               /// For callback functions.

/// @brief A utility class to manage configurations and error states.
class ConfigHandler final {
private:
  static constexpr uint8_t maxWifiSsidSize = 24U;                     // Maximum size of the Wi-Fi SSID string.
  static constexpr uint8_t maxWifiPasswordSize = 24U;                 // Maximum size of the Wi-Fi password string.
  static constexpr uint8_t maxMqttUserNameSize = 24U;                 // Maximum size of the MQTT user name string.
  static constexpr uint8_t maxMqttPasswordSize = 24U;                 // Maximum size of the MQTT password string.
  static constexpr uint8_t maxMqttServerUrlSize = 32U;                // Maximum size of the MQTT server URL string.

  /// @brief Delete constructor.
  ConfigHandler() = delete;

  /// @brief Delete destructor.
  ~ConfigHandler() = delete;

public:
  /// @brief Initializes the file system and retrieves usage statistics.
  /// @param totalBytes Reference to a variable where the total file system capacity (in bytes) will be stored.
  /// @param usedBytes Reference to a variable where the used space (in bytes) will be stored.
  /// @param freeBytes Reference to a variable where the free space (in bytes) will be stored.
  /// @return `true` if the initialization was successful; `false` otherwise.
  [[nodiscard]] static bool initialiseFileSystem(size_t& totalBytes, size_t& usedBytes, size_t& freeBytes);

  /// @brief Gets the maximum allowed size for the Wi-Fi SSID.
  /// @return Maximum size of the SSID string.
  [[nodiscard]] static constexpr uint8_t getMaxWifiSsidSize() { return maxWifiSsidSize; }

  /// @brief Gets the maximum allowed size for the Wi-Fi password.
  /// @return Maximum size of the password string.
  [[nodiscard]] static constexpr uint8_t getMaxWifiPasswordSize() { return maxWifiPasswordSize; }

  /// @brief Retrieves Wi-Fi configuration (SSID and password) from a file.
  /// @param ssid Buffer to store the retrieved SSID.
  /// @param password Buffer to store the retrieved password.
  /// @return Error state, where `0` means success.
  [[nodiscard]] static uint8_t getWifiConfig(char (&ssid)[maxWifiSsidSize], char (&password)[maxWifiPasswordSize]);

  /// @brief Retrieves the server certificate using a callback for storage.
  /// @param storeCert A callback function to handle the storage of the certificate.
  /// The callback receives a reference to a `Stream` and the certificate size in bytes.
  /// @return Error state, where `0` means success.
  [[nodiscard]] static uint8_t getServerCert(std::function<bool(Stream&, size_t)> storeCert);

  /// @brief Gets the maximum allowed size for the MQTT user name.
  /// @return Maximum size of the MQTT user name string.
  [[nodiscard]] static constexpr uint8_t getMaxMqttUserNameSize() { return maxMqttUserNameSize; }

  /// @brief Gets the maximum allowed size for the MQTT password.
  /// @return Maximum size of the MQTT password string.
  [[nodiscard]] static constexpr uint8_t getMaxMqttPasswordSize() { return maxMqttPasswordSize; }

  /// @brief Gets the maximum allowed size for the MQTT server URL.
  /// @return Maximum size of the MQTT server URL string.
  [[nodiscard]] static constexpr uint8_t getMaxMqttServerUrlSize() { return maxMqttServerUrlSize; }

  /// @brief Retrieves server credentials (MQTT username, password, URL, and port) from a file.
  /// @param mqttUserName Buffer to store the MQTT user name.
  /// @param mqttPassword Buffer to store the MQTT password.
  /// @param mqttServerUrl Buffer to store the MQTT server URL.
  /// @param mqttServerPort Variable to store the MQTT server port number.
  /// @return Error state, where `0` means success.
  [[nodiscard]] static uint16_t getServerCredentials(char (&mqttUserName)[maxMqttUserNameSize], char (&mqttPassword)[maxMqttPasswordSize],
    char (&mqttServerUrl)[maxMqttServerUrlSize], uint16_t &mqttServerPort);

  ConfigHandler(const ConfigHandler&) = delete;                       // Define copy constructor.
  ConfigHandler& operator=(const ConfigHandler&) = delete;            // Define copy assignment operator.
  ConfigHandler(ConfigHandler&&) = delete;                            // Define move constructor.
  ConfigHandler& operator=(ConfigHandler&&) = delete;                 // Define move assignment operator

private:
  /// @brief Enumeration representing possible error states for Wi-Fi configuration.
  enum class WifiConfigError : uint8_t {
    NONE                  = 0U,                   // No error.
    NO_WIFI_CONFIG_FILE   = 1 << 0U,              // Wi-Fi configuration file is missing.
    CANNOT_OPEN_FILE      = 1 << 1U,              // Unable to open the configuration file.
    JSON_PARSING_ERROR    = 1 << 2U,              // JSON parsing failed.
    MISSING_SSID_KEY      = 1 << 3U,              // SSID key is missing in the JSON.
    MISSING_PWD_KEY       = 1 << 4U,              // Password key is missing in the JSON.
    SSID_LENGTH_ERR       = 1 << 5U,              // SSID length is invalid.
    PWD_LENGTH_ERR        = 1 << 6U               // Password length is invalid.
  };

  /// @brief Enumeration representing possible error states for server certificate retrieval.
  enum class ServerCertError : uint8_t {
    NONE                  = 0U,                   // No error.
    NO_SERVER_CERT_FILE   = 1 << 0U,              // Server certification file is missing.
    CANNOT_OPEN_FILE      = 1 << 1U,              // Unable to open the configuration file.
    CERT_FILE_EMPTY       = 1 << 2U,              // Server certificate file is empty.
    CALLBACK_NULLPTR      = 1 << 3U,              // Callback function is nullptr.
    CERT_STORING_FAILED   = 1 << 4U               // Unable to store the certificate.
  };

  /// @brief Enumeration representing possible error states for server credentials retrieval.
  enum class ServerCredError : uint16_t {
    NONE                  = 0U,                   // No error.
    NO_SERVER_CRED_FILE   = 1 << 0U,              // Server credentials file is missing.
    CANNOT_OPEN_FILE      = 1 << 1U,              // Unable to open the credentials file.
    JSON_PARSING_ERROR    = 1 << 2U,              // JSON parsing failed.
    MISSING_MQTT_USER     = 1 << 3U,              // MQTT user name is missing.
    MISSING_MQTT_PASS     = 1 << 4U,              // MQTT password is missing.
    MISSING_MQTT_URL      = 1 << 5U,              // MQTT server URL is missing.
    MISSING_MQTT_PORT     = 1 << 6U,              // MQTT server port number is missing.
    MQTT_USER_LENGTH_ERR  = 1 << 7U,              // MQTT user name length is invalid.
    MQTT_PASS_LENGTH_ERR  = 1 << 8U,              // MQTT password length is invalid.
    MQTT_URL_LENGTH_ERR   = 1 << 9U,              // MQTT server URL length is invalid.
    MQTT_PORT_NUM_ERR     = 1 << 10U              // MQTT server port number is invalid.
  };
};
#endif // CONFIG_HANDLER_HPP