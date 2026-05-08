#ifndef NETWORK_MANAGER_HPP
#define NETWORK_MANAGER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "common.hpp"                                               /// Common definitions and functions.
#ifdef ESP8266
#include <ESP8266WiFi.h>                                            /// WiFi driver for ESP8266.
#include <ENC28J60lwIP.h>                                           /// Ethernet driver for ENC28J60 on ESP8266.
#include <optional>                                                 /// Optional instances.
#elif defined ESP32
#include <WiFi.h>                                                   /// WiFi driver for ESP32.
#include <ETH.h>                                                    /// Ethernet driver for ESP32.
#endif

/// @brief Manages network interfaces and connectivity for ESP-based devices.
class NetworkManager final {
private:
  using NetworkErrorType = uint16_t;                                // Underlying type for network error states.
  static constexpr uint8_t macAddressSize = 6U;                     // Size of the MAC address array.
  static constexpr uint8_t invalidPin = 0xFF;                       // Invalid pin value.
  static constexpr char hostnamePrefix[] = "project_";              // PIO env prefix stripped from the hostname.
  static constexpr uint8_t macSuffixBytes = 3U;                     // Number of MAC bytes (from the end) appended to the hostname.
  static constexpr uint8_t hostnameLen =
    static_cast<uint8_t>(Build::getPioEnvLength()
      - (static_cast<uint8_t>(sizeof(hostnamePrefix)) - 1U)  // subtract prefix chars (sizeof includes null, so -1)
      + 1U                             // underscore separator
      + macSuffixBytes * 2U            // 2 hex digits per MAC byte
      + 1U);                           // null terminator
#ifdef ESP32
  static constexpr uint8_t ethPhyAddress = 1U;                      // I²C-address of Ethernet PHY (0 or 1 for LAN8720, 31 for TLK110)
  static constexpr int32_t ethPhyPower = 17;                        // Pin# of the enable signal for the external crystal oscillator (-1 to disable for internal APLL source)
  static constexpr int32_t ethPhyMdcPin = 23;                       // Pin# of the I²C clock signal for the Ethernet PHY
  static constexpr int32_t ethPhyMdioPin = 18;                      // Pin# of the I²C IO signal for the Ethernet PHY
  static constexpr eth_phy_type_t ethPhyType = ETH_PHY_LAN8720;     // Type of the Ethernet PHY (LAN8720 or TLK110)
  static constexpr eth_clock_mode_t ethClockMode = ETH_CLOCK_GPIO0_IN;
  // ETH_CLOCK_GPIO0_IN   - default: external clock from crystal oscillator
  // ETH_CLOCK_GPIO0_OUT  - 50MHz clock from internal APLL output on GPIO0 - possibly an inverter is needed for LAN8720
  // ETH_CLOCK_GPIO16_OUT - 50MHz clock from internal APLL output on GPIO16 - possibly an inverter is needed for LAN8720
  // ETH_CLOCK_GPIO17_OUT - 50MHz clock from internal APLL inverted output on GPIO17 - tested with LAN8720
#endif

  // Wi-Fi status strings
  static constexpr const char PROGMEM wlNoShieldStr[]        = "WL_NO_SHIELD";
  static constexpr const char PROGMEM wlIdleStatusStr[]      = "WL_IDLE_STATUS";
  static constexpr const char PROGMEM wlNoSsidAvailableStr[] = "WL_NO_SSID_AVAIL";
  static constexpr const char PROGMEM wlScanCompletedStr[]   = "WL_SCAN_COMPLETED";
  static constexpr const char PROGMEM wlConnectedStr[]       = "WL_CONNECTED";
  static constexpr const char PROGMEM wlConnectFailedStr[]   = "WL_CONNECT_FAILED";
  static constexpr const char PROGMEM wlConnectionLostStr[]  = "WL_CONNECTION_LOST";
  static constexpr const char PROGMEM wlWrongPasswordStr[]   = "WL_WRONG_PASSWORD";
  static constexpr const char PROGMEM wlDisconnectedStr[]    = "WL_DISCONNECTED";
  static constexpr const char PROGMEM wlUnknownStatusStr[]   = "WL_UNKNOWN_STATUS";

public:
  /// @brief Represents supported network interfaces.
  enum class Interface : uint8_t {
    UNKNOWN = 0U,                                 // No interface specified.
    WIFI,                                         // Wi-Fi interface.
    ENC28J60,                                     // ENC28J60 Ethernet interface.
    LAN8720                                       // LAN8720 Ethernet interface.
  };

  /// @brief Constructs a NetworkManager instance.
  /// @param interface Initial network interface to configure.
  /// @param ethernetShieldCsPin Chip Select pin for ENC28J60 (optional).
  NetworkManager(Interface interface, uint8_t ethernetShieldCsPin = invalidPin);

  /// @brief Default destructor.
  ~NetworkManager() = default;

  /// @brief Sets the network interface.
  /// @param interface Network interface to configure.
  /// @param ethernetShieldCsPin Chip Select pin for ENC28J60 (optional).
  void setNetworkInterface(Interface interface, uint8_t ethernetShieldCsPin = invalidPin);

  /// @brief Connects to the configured network interface.
  /// @return A bitfield representing network error states.
  [[nodiscard]] NetworkErrorType connect();

  /// @brief Checks if the network connection is available.
  /// @return True if the network is connected, false otherwise.
  [[nodiscard]] bool isNetworkAvailable();

  /// @brief Retrieves the MAC address as an array of bytes.
  /// @param[out] macAddress A reference to an array of size macAddressSize where the MAC address will be stored.
  /// @return True if the MAC address was successfully retrieved, false if the MAC address is uninitialized (all zeros).
  [[nodiscard]] bool getMacAddress(uint8_t (&macAddress)[macAddressSize]);

  NetworkManager(const NetworkManager&) = delete;                       // Define copy constructor.
  NetworkManager& operator=(const NetworkManager&) = delete;            // Define copy assignment operator.
  NetworkManager(NetworkManager&&) = delete;                            // Define move constructor.
  NetworkManager& operator=(NetworkManager&&) = delete;                 // Define move assignment operator

private:
  /// @brief Converts an internal Wi-Fi status to a string.
  /// @param status The Wi-Fi status to convert.
  /// @return A string representation of the Wi-Fi status.
  [[nodiscard]] static const char* getIntStatusStr(wl_status_t status);

  /// @brief Builds a unique hostname from the PIO env name (without "project_" prefix) and the last 4 MAC bytes.
  /// Must be called after mac[] is populated. Result is stored in hostnameBuffer.
  void buildHostname();
#ifdef ESP32
  /// @brief Handles ESP32-specific Ethernet events.
  /// @param event The Wi-Fi event type.
  static void WiFiEvent(WiFiEvent_t event);
#endif
  // Network error types as bitfields.
  enum class NetworkError : NetworkErrorType {
    NONE                  = 0U,                   // No error.
    INVALID_INTERFACE     = 1 << 0U,              // Invalid network interface selected.
    WIFI_INIT_FAILED      = 1 << 1U,              // Wi-Fi initialization failed.
    WIFI_CONFIG_ERROR     = 1 << 2U,              // Wi-Fi configuration error.
    ENC28J60_NO_DRIVER    = 1 << 3U,              // ENC28J60 driver not initialized.
    ENC28J60_INIT_FAILED  = 1 << 4U,              // ENC28J60 initialization failed.
    LAN8720_INIT_FAILED   = 1 << 5U,              // LAN8720 initialization failed.
    MAC_ADDRESS_INVALID   = 1 << 6U               // Invalid MAC address.
  };

#ifdef ESP32
  static volatile bool ethConnected;                                // Ethernet connection status.
#elif defined ESP8266
  std::optional<ENC28J60lwIP> ethernetEnc28j60;                     // Optional instance for ENC28J60 on ESP8266.
#endif
  Interface networkInterface;                                       // Current network interface.
  wl_status_t interfaceStatus;                                      // Current network interface status.
  uint8_t mac[macAddressSize];                                      // Byte array to store the MAC address.
  char hostnameBuffer[hostnameLen]{};                               // Unique hostname: env (no prefix) + "_" + last 4 MAC bytes.
};
#endif // NETWORK_MANAGER_HPP