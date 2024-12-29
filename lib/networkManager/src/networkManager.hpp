#ifndef NETWORK_MANAGER_HPP
#define NETWORK_MANAGER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <HardwareSerial.h>                                         /// Hardware serial driver for communication with peripheral devices.
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
  using NetworkErrorType = uint16_t;              // Underlying type for network error states.

public:
  /// @brief Represents supported network interfaces.
  enum class Interface : uint8_t {
    UNKNOWN = 0U,                                 // No interface specified.
    WIFI,                                         // Wi-Fi interface.
    ENC28J60,                                     // ENC28J60 Ethernet interface.
    LAN8720                                       // LAN8720 Ethernet interface.
  };

  /// @brief Constructs a NetworkManager instance.
  /// @param serial HardwareSerial instance for logs.
  /// @param interface Initial network interface to configure.
  /// @param ethernetShieldCsPin Chip Select pin for ENC28J60 (optional).
  NetworkManager(HardwareSerial& serial, Interface interface, uint8_t ethernetShieldCsPin = invalidPin);

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

  /// @brief Gets the MAC address as a string.
  /// @return A pointer to the MAC address string.
  [[nodiscard]] const char* getMacAddressString() { return macAddressStr; }

  NetworkManager(const NetworkManager&) = delete;                       // Define copy constructor.
  NetworkManager& operator=(const NetworkManager&) = delete;            // Define copy assignment operator.
  NetworkManager(NetworkManager&&) = delete;                            // Define move constructor.
  NetworkManager& operator=(NetworkManager&&) = delete;                 // Define move assignment operator

private:
  /// @brief Converts an internal Wi-Fi status to a string.
  /// @param status The Wi-Fi status to convert.
  /// @return A string representation of the Wi-Fi status.
  [[nodiscard]] const char* getIntStatusStr(wl_status_t status);
#ifdef ESP32
  /// @brief Handles ESP32-specific Ethernet events.
  /// @param event The Wi-Fi event type.
  static void WiFiEvent(WiFiEvent_t event);
#endif
  // Network error types as bitfields.
  enum class NetworkError : NetworkErrorType {
    NONE                  = 0U,                   // No error.
    NO_INTERFACE_SET      = 1 << 0U,              // No network interface is set.
    INVALID_INTERFACE     = 1 << 1U,              // Invalid network interface selected.
    WIFI_INIT_FAILED      = 1 << 2U,              // Wi-Fi initialization failed.
    WIFI_CONFIG_ERROR     = 1 << 3U,              // Wi-Fi configuration error.
    WIFI_CONN_FAILED      = 1 << 4U,              // Wi-Fi connection failed.
    ENC28J60_NO_DRIVER    = 1 << 5U,              // ENC28J60 driver not initialized.
    ENC28J60_INIT_FAILED  = 1 << 6U,              // ENC28J60 initialization failed.
    ENC28J60_CONN_FAILED  = 1 << 7U,              // ENC28J60 connection failed.
    LAN8720_INIT_FAILED   = 1 << 8U,              // LAN8720 initialization failed.
    LAN8720_CONN_FAILED   = 1 << 9U,              // LAN8720 connection failed.
    MAC_STRING_INVALID    = 1 << 10U              // Invalid MAC address string.
  };

  // Constants
  static constexpr uint8_t macAddressStrSize = 13U;           // Size of the MAC address string buffer.
  static constexpr uint8_t invalidPin = 0xFF;                 // Invalid pin value.

  // Wi-Fi status strings
  static const char PROGMEM wlNoShieldStr[];
  static const char PROGMEM wlIdleStatusStr[];
  static const char PROGMEM wlNoSsidAvailableStr[];
  static const char PROGMEM wlScanCompletedStr[];
  static const char PROGMEM wlConnectedStr[];
  static const char PROGMEM wlConnectFailedStr[];
  static const char PROGMEM wlConnectionLostStr[];
  static const char PROGMEM wlWrongPasswordStr[];
  static const char PROGMEM wlDisconnectedStr[];
  static const char PROGMEM wlUnknownStatusStr[];

#ifdef ESP8266
  std::optional<ENC28J60lwIP> ethernetEnc28j60;               // Optional instance for ENC28J60 on ESP8266.
#elif defined ESP32
  static constexpr uint8_t ETH_PHY_ADDR_ = 1;                 // I²C-address of Ethernet PHY (0 or 1 for LAN8720, 31 for TLK110)
  static constexpr int8_t ETH_PHY_POWER_ = 17;                // Pin# of the enable signal for the external crystal oscillator (-1 to disable for internal APLL source)
  static constexpr int8_t ETH_PHY_MDC_ = 23;                  // Pin# of the I²C clock signal for the Ethernet PHY
  static constexpr int8_t ETH_PHY_MDIO_ = 18;                 // Pin# of the I²C IO signal for the Ethernet PHY
  static constexpr auto ETH_PHY_TYPE_ = ETH_PHY_LAN8720;      // Type of the Ethernet PHY (LAN8720 or TLK110)
  static constexpr auto ETH_CLK_MODE_ = ETH_CLOCK_GPIO0_IN;
  // ETH_CLOCK_GPIO0_IN   - default: external clock from crystal oscillator
  // ETH_CLOCK_GPIO0_OUT  - 50MHz clock from internal APLL output on GPIO0 - possibly an inverter is needed for LAN8720
  // ETH_CLOCK_GPIO16_OUT - 50MHz clock from internal APLL output on GPIO16 - possibly an inverter is needed for LAN8720
  // ETH_CLOCK_GPIO17_OUT - 50MHz clock from internal APLL inverted output on GPIO17 - tested with LAN8720
  static volatile bool ethConnected;                          // Ethernet connection status.
#endif
  HardwareSerial& serial;                                     // Serial instance for debug logs.
  Interface networkInterface;                                 // Current network interface.
  wl_status_t interfaceStatus;                                // Current network interface status.
  char macAddressStr[macAddressStrSize];                      // Buffer for the MAC address string.
};
#endif // NETWORK_MANAGER_HPP