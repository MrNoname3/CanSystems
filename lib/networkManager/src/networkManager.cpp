#include "networkManager.hpp"
#include "common.hpp"                                               /// Common definitions and functions.
#include "configHandler.hpp"

const char NetworkManager::wlNoShieldStr[] PROGMEM            = "WL_NO_SHIELD";
const char NetworkManager::wlIdleStatusStr[] PROGMEM          = "WL_IDLE_STATUS";
const char NetworkManager::wlNoSsidAvailableStr[] PROGMEM     = "WL_NO_SSID_AVAIL";
const char NetworkManager::wlScanCompletedStr[] PROGMEM       = "WL_SCAN_COMPLETED";
const char NetworkManager::wlConnectedStr[] PROGMEM           = "WL_CONNECTED";
const char NetworkManager::wlConnectFailedStr[] PROGMEM       = "WL_CONNECT_FAILED";
const char NetworkManager::wlConnectionLostStr[] PROGMEM      = "WL_CONNECTION_LOST";
const char NetworkManager::wlWrongPasswordStr[] PROGMEM       = "WL_WRONG_PASSWORD";
const char NetworkManager::wlDisconnectedStr[] PROGMEM        = "WL_DISCONNECTED";
const char NetworkManager::wlUnknownStatusStr[] PROGMEM       = "WL_UNKNOWN_STATUS";
#ifdef ESP32
volatile bool NetworkManager::ethConnected = false;
#endif

NetworkManager::NetworkManager(HardwareSerial& serial, Interface interface, uint8_t ethernetShieldCsPin) :
#ifdef ESP8266
  ethernetEnc28j60{},
#endif
  serial(serial),
  networkInterface(Interface::UNKNOWN),
  interfaceStatus(WL_DISCONNECTED),
  mac{0U}
{
  setNetworkInterface(interface, ethernetShieldCsPin);
}

void NetworkManager::setNetworkInterface(Interface interface, uint8_t ethernetShieldCsPin) {
  if(interface == Interface::UNKNOWN) { return; }
#ifdef ESP8266
  if((interface == Interface::ENC28J60) && (ethernetShieldCsPin != invalidPin)) {
    ethernetEnc28j60.emplace(ethernetShieldCsPin);
    if(!ethernetEnc28j60.has_value()) { return; }
  }
#endif
  networkInterface = interface;
}

NetworkManager::NetworkErrorType NetworkManager::connect() {
  ErrorState<NetworkError, NetworkErrorType> networkErrState;
  serial.printf_P(PSTR("[NETWORK] Network interface: "));
  switch(networkInterface) {
    case Interface::WIFI: {
      serial.printf_P(PSTR("[Wi-Fi]\r\n"));
      WiFi.macAddress(mac);
      const bool wifiInit = WiFi.mode(WIFI_STA);
      serial.printf_P(PSTR("[NETWORK] Initialising Wi-Fi: %s\r\n"), Str::getStateStr(wifiInit));
      if(!wifiInit) {
        networkErrState.setError(NetworkError::WIFI_INIT_FAILED);
        return networkErrState.getRawErrorState();
      }
      WiFi.setAutoReconnect(true);
      char ssid[ConfigHandler::getMaxWifiSsidSize()] = {'\0'};
      char password[ConfigHandler::getMaxWifiPasswordSize()] = {'\0'};
      const uint8_t wifiConfigResult = ConfigHandler::getWifiConfig(ssid, password);
      const bool wifiConfigOk = (wifiConfigResult == 0U);
      serial.printf_P(PSTR("[NETWORK] Wifi config: %s\r\n"), Str::getStateStr(wifiConfigOk));
      if(!wifiConfigOk) {
        serial.printf_P(PSTR("  Code: %hu\r\n"), wifiConfigResult);
        networkErrState.setError(NetworkError::WIFI_CONFIG_ERROR);
        return networkErrState.getRawErrorState();
      } else {
        WiFi.begin(ssid, password);
      }
      serial.printf_P(PSTR("[NETWORK] Connecting to router...\r\n"));
      while(WiFi.status() != WL_CONNECTED) {
        yield();
      }
      if(WiFi.status() != WL_CONNECTED) {
        networkErrState.setError(NetworkError::WIFI_CONN_FAILED);
        return networkErrState.getRawErrorState();
      }
      serial.printf_P(PSTR("  IP: %s\r\n"), WiFi.localIP().toString().c_str());
      serial.printf_P(PSTR("  GW: %s\r\n"), WiFi.gatewayIP().toString().c_str());
      serial.printf_P(PSTR("  SNM: %s\r\n"), WiFi.subnetMask().toString().c_str());
    } break;
#ifdef ESP8266
    case Interface::ENC28J60: {
      serial.printf_P(PSTR("[ENC28J60]\r\n"));
      if(!ethernetEnc28j60.has_value()) {
        networkErrState.setError(NetworkError::ENC28J60_NO_DRIVER);
        return networkErrState.getRawErrorState();
      }
      WiFi.macAddress(mac);
      WiFi.mode(WIFI_OFF);
      ethernetEnc28j60.value().setDefault();         // default route set through this interface
      const bool ethInit = ethernetEnc28j60.value().begin(mac);
      serial.printf_P(PSTR("[NETWORK] Initialising ethernet modul: %s\r\n"), Str::getStateStr(ethInit));
      if(!ethInit) {
        networkErrState.setError(NetworkError::ENC28J60_INIT_FAILED);
        return networkErrState.getRawErrorState();
      }
      serial.printf_P(PSTR("[NETWORK] Connecting to router...\r\n"));
      while(!ethernetEnc28j60.value().connected()) {
        yield();
      }
      if(!ethernetEnc28j60.value().connected()) {
        networkErrState.setError(NetworkError::ENC28J60_CONN_FAILED);
        return networkErrState.getRawErrorState();
      }
      serial.printf_P(PSTR("  IP: %s\r\n"), ethernetEnc28j60.value().localIP().toString().c_str());
      serial.printf_P(PSTR("  GW: %s\r\n"), ethernetEnc28j60.value().gatewayIP().toString().c_str());
      serial.printf_P(PSTR("  SNM: %s\r\n"), ethernetEnc28j60.value().subnetMask().toString().c_str());
    } break;
#elif defined ESP32
    case Interface::LAN8720: {
      serial.printf_P(PSTR("[LAN8720]\r\n"));
      WiFi.mode(WIFI_OFF);
      WiFi.onEvent(NetworkManager::WiFiEvent);
      const bool ethInit = ETH.begin(ethPhyAddress, ethPhyPower, ethPhyMdcPin, ethPhyMdioPin, ethPhyType, ethClockMode);
      serial.printf_P(PSTR("[NETWORK] Initialising ethernet modul: %s\r\n"), Str::getStateStr(ethInit));
      if(!ethInit) {
        networkErrState.setError(NetworkError::LAN8720_INIT_FAILED);
        return networkErrState.getRawErrorState();
      }
      serial.printf_P(PSTR("[NETWORK] Connecting to router...\r\n"));
      while(!ethConnected) {    // Wait until the device receives an IP address.
        yield();
      }
      if(!ethConnected) {
        networkErrState.setError(NetworkError::LAN8720_CONN_FAILED);
        return networkErrState.getRawErrorState();
      }
      serial.printf_P(PSTR("  IP: %s\r\n"), ETH.localIP().toString().c_str());
      serial.printf_P(PSTR("  GW: %s\r\n"), ETH.gatewayIP().toString().c_str());
      serial.printf_P(PSTR("  SNM: %s\r\n"), ETH.subnetMask().toString().c_str());
      ETH.setHostname(Build::getPioEnv());
      ETH.macAddress(mac);
    } break;
#endif
    default: {
      serial.printf_P(PSTR("[INVALID]\r\n"));
      networkErrState.setError(NetworkError::INVALID_INTERFACE);
      return networkErrState.getRawErrorState();
    } break;
  }
  serial.printf_P(PSTR("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  const bool macValid = (memcmp(mac, "\0\0\0\0\0\0", sizeof(mac)) != 0);
  if(!macValid) {
    serial.printf_P(PSTR("[NETWORK] MAC address invalid!\r\n"));
    networkErrState.setError(NetworkError::MAC_ADDRESS_INVALID);
    return networkErrState.getRawErrorState();
  }

  interfaceStatus = WL_CONNECTED;
  return networkErrState.getRawErrorState();
}

bool NetworkManager::isNetworkAvailable() {
  yield();
  wl_status_t actualInterfaceStatus = WL_DISCONNECTED;
  switch(networkInterface) {
    case Interface::WIFI: {
      actualInterfaceStatus = WiFi.status();
    } break;
#ifdef ESP8266
    case Interface::ENC28J60: {
      actualInterfaceStatus = ethernetEnc28j60.value().status();
    } break;
#elif defined ESP32
    case Interface::LAN8720: {
      actualInterfaceStatus = ethConnected ? WL_CONNECTED : WL_DISCONNECTED;
    } break;
#endif
    default: {
      return false;
    } break;
  }
  if(interfaceStatus != actualInterfaceStatus) {
    serial.printf_P(PSTR("[NETWORK] Status changed: %s -> %s\r\n"), getIntStatusStr(interfaceStatus), getIntStatusStr(actualInterfaceStatus));
    interfaceStatus = actualInterfaceStatus;
  }
  return (interfaceStatus == WL_CONNECTED);
}

bool NetworkManager::getMacAddress(uint8_t (&macAddress)[macAddressSize]) {
  memcpy(macAddress, mac, sizeof(mac));
  if((memcmp(macAddress, "\0\0\0\0\0\0", sizeof(macAddress)) == 0)) { return false; }
  return true;
}

const char* NetworkManager::getIntStatusStr(wl_status_t status) {
  switch(status) {
    case WL_NO_SHIELD: { return wlNoShieldStr; } break;
    case WL_IDLE_STATUS: { return wlIdleStatusStr; } break;
    case WL_NO_SSID_AVAIL: { return wlNoSsidAvailableStr; } break;
    case WL_SCAN_COMPLETED: { return wlScanCompletedStr; } break;
    case WL_CONNECTED: { return wlConnectedStr; } break;
    case WL_CONNECT_FAILED: { return wlConnectFailedStr; } break;
    case WL_CONNECTION_LOST: { return wlConnectionLostStr; } break;
#ifdef ESP8266
    case WL_WRONG_PASSWORD: { return wlWrongPasswordStr; } break;
#endif
    case WL_DISCONNECTED: { return wlDisconnectedStr; } break;
    default: { return wlUnknownStatusStr; } break;
  }
}

#ifdef ESP32
void NetworkManager::WiFiEvent(WiFiEvent_t event) {
  switch(event) {
    case ARDUINO_EVENT_ETH_START: {} break;
    case ARDUINO_EVENT_ETH_CONNECTED: {} break;
    case ARDUINO_EVENT_ETH_GOT_IP: { ethConnected = true; } break;
    case ARDUINO_EVENT_ETH_DISCONNECTED: { ethConnected = false; } break;
    case ARDUINO_EVENT_ETH_STOP: { ethConnected = false; } break;
    default: {} break;
  }
}
#endif