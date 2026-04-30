#ifndef CONFIG_HANDLER_HPP
#define CONFIG_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <type_traits>                                              /// For static_assert type checks in getJsonValue.
#include "common.hpp"                                               /// Common definitions and functions.
#include <LittleFS.h>                                               /// For File and Stream types used in getServerCert.
#include <ArduinoJson.h>                                            /// For JsonDocument and JsonVariant used in JSON helpers.

/// @brief A utility class to manage configurations and error states.
class ConfigHandler final {
private:
  using WifiConfigErrorType = uint8_t;                                // Underlying type for Wi-Fi config error states.
  using ServerCertErrorType = uint8_t;                                // Underlying type for server certificate error states.
  using ServerCredErrorType = uint16_t;                               // Underlying type for server credentials error states.
  static constexpr uint8_t maxWifiSsidSize = 24U;                     // Maximum size of the Wi-Fi SSID string.
  static constexpr uint8_t maxWifiPasswordSize = 24U;                 // Maximum size of the Wi-Fi password string.
  static constexpr uint8_t maxMqttUserNameSize = 24U;                 // Maximum size of the MQTT user name string.
  static constexpr uint8_t maxMqttPasswordSize = 24U;                 // Maximum size of the MQTT password string.
  static constexpr uint8_t maxMqttServerUrlSize = 32U;                // Maximum size of the MQTT server URL string.

public:
  /// @brief Result of a `loadJsonFile` call.
  enum class JsonLoadResult : uint8_t { Ok, FileOpenFailed, ParseFailed };

  /// @brief Initializes the file system and retrieves usage statistics.
  /// @param totalBytes Reference to a variable where the total file system capacity (in bytes) will be stored.
  /// @param usedBytes Reference to a variable where the used space (in bytes) will be stored.
  /// @param freeBytes Reference to a variable where the free space (in bytes) will be stored.
  /// @return `true` if the initialization was successful; `false` otherwise.
  [[nodiscard]] static bool initialiseFileSystem(size_t& totalBytes, size_t& usedBytes, size_t& freeBytes);

  /// @brief Opens a LittleFS file and deserializes its JSON content into `doc`.
  /// @param filePath_P PROGMEM path to the JSON file.
  /// @param doc JsonDocument to populate; must outlive any use of its contents.
  /// @return `JsonLoadResult::Ok` on success; `FileOpenFailed` or `ParseFailed` otherwise.
  [[nodiscard]] static JsonLoadResult loadJsonFile(const char* filePath_P, JsonDocument& doc);

  /// @brief Opens a LittleFS JSON file and returns the value of a single key.
  /// @tparam T Value type to extract. Pointer types (e.g. `const char*`) are forbidden at
  ///           compile time — use `loadJsonFile` instead when you need string values.
  /// @param filePath_P PROGMEM path to the JSON file.
  /// @param key_P PROGMEM key to look up in the JSON object.
  /// @param outValue Reference that receives the extracted value on success.
  /// @return `true` if the file was parsed and the key was found with the expected type.
  template<typename T>
  [[nodiscard]] static bool getJsonValue(const char* filePath_P, const char* key_P, T& outValue) {
    static_assert(!std::is_pointer<T>::value,
      "getJsonValue: pointer types are unsafe (dangling pointer); use loadJsonFile for string values");
    JsonDocument doc;
    if(loadJsonFile(filePath_P, doc) != JsonLoadResult::Ok) { return false; }
    JsonVariant var = doc[FPSTR(key_P)];
    const bool typeOk = var.is<T>();
    if(typeOk) { outValue = var.as<T>(); }
    return typeOk;
  }

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
  [[nodiscard]] static WifiConfigErrorType getWifiConfig(char (&ssid)[maxWifiSsidSize], char (&password)[maxWifiPasswordSize]);

  /// @brief Retrieves the server certificate using a callback for storage.
  /// @param storeCert A callable (e.g. lambda) invoked with a reference to the certificate `Stream` and its size.
  /// @return Error state, where `0` means success.
  template<typename Callback>
  [[nodiscard]] static ServerCertErrorType getServerCert(Callback storeCert) {
    ErrorState<ServerCertError, ServerCertErrorType> serverCertErrState;
    const char* const certPath = FileName::getMqttServerCertLocation();
    File certFile = LittleFS.open(FPSTR(certPath), "r");
    if(!certFile) {
      serverCertErrState.setError(ServerCertError::FILE_OPEN_FAILED);
      return serverCertErrState.getRawErrorState();
    }
    if(certFile.size() > 0U) {
      const bool certStoringOk = storeCert(certFile, certFile.size());
      if(!certStoringOk) {
        serverCertErrState.setError(ServerCertError::CERT_STORING_FAILED);
      }
    } else {
      serverCertErrState.setError(ServerCertError::CERT_FILE_EMPTY);
    }
    certFile.close();
    return serverCertErrState.getRawErrorState();
  }

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
  [[nodiscard]] static ServerCredErrorType getServerCredentials(char (&mqttUserName)[maxMqttUserNameSize], char (&mqttPassword)[maxMqttPasswordSize],
    char (&mqttServerUrl)[maxMqttServerUrlSize], uint16_t &mqttServerPort);

  ConfigHandler() = delete;                                           // Delete constructor.
  ~ConfigHandler() = delete;                                          // Delete destructor.
  ConfigHandler(const ConfigHandler&) = delete;                       // Define copy constructor.
  ConfigHandler& operator=(const ConfigHandler&) = delete;            // Define copy assignment operator.
  ConfigHandler(ConfigHandler&&) = delete;                            // Define move constructor.
  ConfigHandler& operator=(ConfigHandler&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Enumeration representing possible error states for Wi-Fi configuration.
  enum class WifiConfigError : WifiConfigErrorType {
    NONE                  = 0U,                   // No error.
    FILE_OPEN_FAILED      = 1U << 0U,             // Unable to open the configuration file.
    JSON_PARSING_ERROR    = 1U << 1U,             // JSON parsing failed.
    MISSING_SSID_KEY      = 1U << 2U,             // SSID key is missing in the JSON.
    MISSING_PWD_KEY       = 1U << 3U,             // Password key is missing in the JSON.
    SSID_LENGTH_ERR       = 1U << 4U,             // SSID length is invalid.
    PWD_LENGTH_ERR        = 1U << 5U              // Password length is invalid.
  };

  /// @brief Enumeration representing possible error states for server certificate retrieval.
  enum class ServerCertError : ServerCertErrorType {
    NONE                  = 0U,                   // No error.
    FILE_OPEN_FAILED      = 1U << 0U,             // Unable to open the certificate file.
    CERT_FILE_EMPTY       = 1U << 1U,             // Server certificate file is empty.
    CERT_STORING_FAILED   = 1U << 2U              // Unable to store the certificate.
  };

  /// @brief Enumeration representing possible error states for server credentials retrieval.
  enum class ServerCredError : ServerCredErrorType {
    NONE                  = 0U,                   // No error.
    FILE_OPEN_FAILED      = 1U << 0U,             // Unable to open the credentials file.
    JSON_PARSING_ERROR    = 1U << 1U,             // JSON parsing failed.
    MISSING_MQTT_USER     = 1U << 2U,             // MQTT user name is missing.
    MISSING_MQTT_PASS     = 1U << 3U,             // MQTT password is missing.
    MISSING_MQTT_URL      = 1U << 4U,             // MQTT server URL is missing.
    MISSING_MQTT_PORT     = 1U << 5U,             // MQTT server port number is missing.
    MQTT_USER_LENGTH_ERR  = 1U << 6U,             // MQTT user name length is invalid.
    MQTT_PASS_LENGTH_ERR  = 1U << 7U,             // MQTT password length is invalid.
    MQTT_URL_LENGTH_ERR   = 1U << 8U,             // MQTT server URL length is invalid.
    MQTT_PORT_NUM_ERR     = 1U << 9U              // MQTT server port number is invalid.
  };
};
#endif // CONFIG_HANDLER_HPP