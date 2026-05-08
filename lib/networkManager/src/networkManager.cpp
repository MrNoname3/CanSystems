#include "networkManager.hpp"
#include "common.hpp"                                               /// Common definitions and functions.
#include "configHandler.hpp"

namespace {
  constexpr const char PROGMEM logConnecting[] = "[NETWORK] Connecting to router...\r\n";
  constexpr const char PROGMEM logEthInit[]    = "[NETWORK] Initialising ethernet modul: %s\r\n";
  constexpr const char PROGMEM logIp[]         = "  IP: %s\r\n";
  constexpr const char PROGMEM logGw[]         = "  GW: %s\r\n";
  constexpr const char PROGMEM logSnm[]        = "  SNM: %s\r\n";
} // namespace

#ifdef ESP32
volatile bool NetworkManager::ethConnected = false;
#endif

NetworkManager::NetworkManager(Interface interface, uint8_t ethernetShieldCsPin) :
#ifdef ESP8266
  ethernetEnc28j60{},
#endif
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

void NetworkManager::buildHostname() {
  const char* envName = Build::getPioEnv();
  static constexpr uint8_t prefixLen = sizeof(hostnamePrefix) - 1U;
  if(strncmp(envName, hostnamePrefix, prefixLen) == 0) {
    envName += prefixLen;
  }
  snprintf(hostnameBuffer, sizeof(hostnameBuffer), "%s_%02x%02x%02x",
    envName, mac[3], mac[4], mac[5]);
}

NetworkManager::NetworkErrorType NetworkManager::connect() {
  ErrorState<NetworkError, NetworkErrorType> networkErrState;
  Logger::get().printf_P(PSTR("[NETWORK] Network interface: "));
  switch(networkInterface) {
    case Interface::WIFI: {
      Logger::get().printf_P(PSTR("[Wi-Fi]\r\n"));
      WiFi.macAddress(mac);
      buildHostname();
      const bool wifiInit = WiFi.mode(WIFI_STA);
      Logger::get().printf_P(PSTR("[NETWORK] Initialising Wi-Fi: %s\r\n"), Str::getStateStr(wifiInit));
      if(!wifiInit) {
        networkErrState.setError(NetworkError::WIFI_INIT_FAILED);
        return networkErrState.getRawErrorState();
      }
      WiFi.setAutoReconnect(true);
      WiFi.persistent(false);                           // Credentials stored in LittleFS; prevent redundant flash writes.
      char ssid[ConfigHandler::getMaxWifiSsidSize()] = {'\0'};
      char password[ConfigHandler::getMaxWifiPasswordSize()] = {'\0'};
      const uint8_t wifiConfigResult = ConfigHandler::getWifiConfig(ssid, password);
      const bool wifiConfigOk = (wifiConfigResult == 0U);
      Logger::get().printf_P(PSTR("[NETWORK] Wifi config: %s\r\n"), Str::getStateStr(wifiConfigOk));
      if(!wifiConfigOk) {
        Logger::get().printf_P(Str::getErrCodeFmt(), wifiConfigResult);
        networkErrState.setError(NetworkError::WIFI_CONFIG_ERROR);
        return networkErrState.getRawErrorState();
      }
#ifdef ESP8266
      WiFi.hostname(hostnameBuffer);
#elif defined ESP32
      WiFi.setHostname(hostnameBuffer);
#endif
      WiFi.begin(ssid, password);
      Logger::get().printf_P(logConnecting);
      while(WiFi.status() != WL_CONNECTED) {
        yield();
      }
      Logger::get().printf_P(logIp, WiFi.localIP().toString().c_str());
      Logger::get().printf_P(logGw, WiFi.gatewayIP().toString().c_str());
      Logger::get().printf_P(logSnm, WiFi.subnetMask().toString().c_str());
    } break;
#ifdef ESP8266
    case Interface::ENC28J60: {
      Logger::get().printf_P(PSTR("[ENC28J60]\r\n"));
      if(!ethernetEnc28j60.has_value()) {
        networkErrState.setError(NetworkError::ENC28J60_NO_DRIVER);
        return networkErrState.getRawErrorState();
      }
      WiFi.macAddress(mac);
      buildHostname();
      WiFi.mode(WIFI_OFF);
      ethernetEnc28j60.value().setDefault();         // default route set through this interface
      const bool ethInit = ethernetEnc28j60.value().begin(mac);
      Logger::get().printf_P(logEthInit, Str::getStateStr(ethInit));
      if(!ethInit) {
        networkErrState.setError(NetworkError::ENC28J60_INIT_FAILED);
        return networkErrState.getRawErrorState();
      }
      // Set hostname directly on all lwIP netifs without calling dhcp_renew():
      // begin() creates the ENC28J60 netif but leaves hostname null; calling
      // hostname() here would trigger dhcp_renew() on the freshly-started DHCP
      // state machine (INIT state) and corrupt it, causing a WDT reset.
      for(netif* intf = netif_list; intf != nullptr; intf = intf->next) {
        intf->hostname = hostnameBuffer;
      }
      Logger::get().printf_P(logConnecting);
      while(!ethernetEnc28j60.value().connected()) {
        yield();
      }
      Logger::get().printf_P(logIp, ethernetEnc28j60.value().localIP().toString().c_str());
      Logger::get().printf_P(logGw, ethernetEnc28j60.value().gatewayIP().toString().c_str());
      Logger::get().printf_P(logSnm, ethernetEnc28j60.value().subnetMask().toString().c_str());
    } break;
#elif defined ESP32
    case Interface::LAN8720: {
      Logger::get().printf_P(PSTR("[LAN8720]\r\n"));
      WiFi.mode(WIFI_OFF);
      WiFi.onEvent(NetworkManager::WiFiEvent);
      const bool ethInit = ETH.begin(ethPhyAddress, ethPhyPower, ethPhyMdcPin, ethPhyMdioPin, ethPhyType, ethClockMode);
      Logger::get().printf_P(logEthInit, Str::getStateStr(ethInit));
      if(!ethInit) {
        networkErrState.setError(NetworkError::LAN8720_INIT_FAILED);
        return networkErrState.getRawErrorState();
      }
      ETH.macAddress(mac);
      buildHostname();
      ETH.setHostname(hostnameBuffer);  // before while loop: set before DHCP REQUEST is sent
      Logger::get().printf_P(logConnecting);
      while(!ethConnected) {    // Wait until the device receives an IP address.
        yield();
      }
      Logger::get().printf_P(logIp, ETH.localIP().toString().c_str());
      Logger::get().printf_P(logGw, ETH.gatewayIP().toString().c_str());
      Logger::get().printf_P(logSnm, ETH.subnetMask().toString().c_str());
    } break;
#endif
    default: {
      Logger::get().printf_P(PSTR("[INVALID]\r\n"));
      networkErrState.setError(NetworkError::INVALID_INTERFACE);
      return networkErrState.getRawErrorState();
    }
  }
  Logger::get().printf_P(PSTR("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  const bool macValid = (memcmp(mac, "\0\0\0\0\0\0", sizeof(mac)) != 0);
  if(!macValid) {
    Logger::get().printf_P(PSTR("[NETWORK] MAC address invalid!\r\n"));
    networkErrState.setError(NetworkError::MAC_ADDRESS_INVALID);
    return networkErrState.getRawErrorState();
  }

  interfaceStatus = WL_CONNECTED;
  return networkErrState.getRawErrorState();
}

bool NetworkManager::isNetworkAvailable() {
  yield();                                             // Keeps the network stack alive and processes pending events.
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
    }
  }
  if(interfaceStatus != actualInterfaceStatus) {
    Logger::get().printf_P(PSTR("[NETWORK] Status changed: %s -> %s\r\n"), getIntStatusStr(interfaceStatus), getIntStatusStr(actualInterfaceStatus));
    interfaceStatus = actualInterfaceStatus;
  }
  return (interfaceStatus == WL_CONNECTED);
}

bool NetworkManager::getMacAddress(uint8_t (&macAddress)[macAddressSize]) {
  memcpy(macAddress, mac, sizeof(mac));
  return memcmp(macAddress, "\0\0\0\0\0\0", sizeof(macAddress)) != 0;
}

const char* NetworkManager::getIntStatusStr(wl_status_t status) { // NOLINT(readability-convert-member-functions-to-static)
  switch(status) {
    case WL_NO_SHIELD:       { return wlNoShieldStr; }
    case WL_IDLE_STATUS:     { return wlIdleStatusStr; }
    case WL_NO_SSID_AVAIL:   { return wlNoSsidAvailableStr; }
    case WL_SCAN_COMPLETED:  { return wlScanCompletedStr; }
    case WL_CONNECTED:       { return wlConnectedStr; }
    case WL_CONNECT_FAILED:  { return wlConnectFailedStr; }
    case WL_CONNECTION_LOST: { return wlConnectionLostStr; }
#ifdef ESP8266
    case WL_WRONG_PASSWORD:  { return wlWrongPasswordStr; }
#endif
    case WL_DISCONNECTED:    { return wlDisconnectedStr; }
    default:                 { return wlUnknownStatusStr; }
  }
}

#ifdef ESP32
void NetworkManager::WiFiEvent(WiFiEvent_t event) { // NOLINT(readability-convert-member-functions-to-static)
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