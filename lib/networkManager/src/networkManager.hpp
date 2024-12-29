#ifndef NETWORK_MANAGER_HPP
#define NETWORK_MANAGER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <HardwareSerial.h>                                         /// Hardware serial driver for communication with peripheral devices.
#ifdef ESP8266
#include <ESP8266WiFi.h>                                            /// WiFi driver for ESP8266.
#include <ENC28J60lwIP.h>                                           /// Ethernet driver for ENC28J60 on ESP8266.
#include <optional>
#elif defined ESP32
#include <WiFi.h>                                                   /// WiFi driver for ESP32.
#include <ETH.h>                                                    /// Ethernet driver for ESP32.
#endif

class NetworkManager final {
private:
  using NetworkErrorType = uint16_t;

public:
  enum class Interface : uint8_t {
    UNKNOWN = 0U,
    WIFI,
    ENC28J60,
    LAN8720
  };

  NetworkManager(HardwareSerial& serial, Interface interface, uint8_t ethernetShieldCsPin = invalidPin);
  ~NetworkManager() = default;

  void setNetworkInterface(Interface interface, uint8_t ethernetShieldCsPin = invalidPin);
  [[nodiscard]] NetworkErrorType connect();
  [[nodiscard]] bool isNetworkAvailable();
  [[nodiscard]] const char* getMacAddressString() { return macAddressStr; }

  NetworkManager(const NetworkManager&) = delete;                       // Define copy constructor.
  NetworkManager& operator=(const NetworkManager&) = delete;            // Define copy assignment operator.
  NetworkManager(NetworkManager&&) = delete;                            // Define move constructor.
  NetworkManager& operator=(NetworkManager&&) = delete;                 // Define move assignment operator

private:
  [[nodiscard]] const char* getIntStatusStr(wl_status_t status);
#ifdef ESP32
  static void WiFiEvent(WiFiEvent_t event);
#endif

  enum class NetworkError : NetworkErrorType {
    NONE                  = 0U,                   // No error.
    NO_INTERFACE_SET      = 1 << 0U,
    INVALID_INTERFACE     = 1 << 1U,
    WIFI_INIT_FAILED      = 1 << 2U,
    WIFI_CONFIG_ERROR     = 1 << 3U,
    WIFI_CONN_FAILED      = 1 << 4U,
    ENC28J60_NO_DRIVER    = 1 << 5U,
    ENC28J60_INIT_FAILED  = 1 << 6U,
    ENC28J60_CONN_FAILED  = 1 << 7U,
    LAN8720_INIT_FAILED   = 1 << 8U,
    LAN8720_CONN_FAILED   = 1 << 9U,
    MAC_STRING_INVALID    = 1 << 10U
  };

  static constexpr uint8_t macAddressStrSize = 13U;
  static constexpr uint8_t invalidPin = 0xFF;
  static const char PROGMEM networkPrefix[];
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
  std::optional<ENC28J60lwIP> enthernetEnc28j60;
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
  static volatile bool ethConnected;
#endif
  HardwareSerial& serial;
  Interface networkInterface;
  wl_status_t interfaceStatus;
  char macAddressStr[macAddressStrSize];
};
#endif // NETWORK_MANAGER_HPP