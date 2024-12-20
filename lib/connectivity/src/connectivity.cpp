#include "connectivity.hpp"
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include <LittleFS.h>                                               /// Use FLASH filesystem.
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "configHandler.hpp"

#ifdef ESP8266
// Monitor the internal VCC level, it varies with WiFi load.
// Don't connect anything to the analog input pin!
ADC_MODE(ADC_VCC);
#elif defined ESP32
bool Connectivity::ethConnected = false;
#endif
bool Connectivity::isDeviceOnline = true;

const char Connectivity::BASE_TOPIC[] PROGMEM               = "iot";
const char Connectivity::SENDER_TOPIC[] PROGMEM             = "dtos";
const char Connectivity::RECEIVER_TOPIC[] PROGMEM           = "stod";
const char Connectivity::INIT_PREFIX[] PROGMEM              = "[INIT] ";
const char Connectivity::FS_PREFIX[] PROGMEM                = "[FS] ";
const char Connectivity::ETH_PREFIX[] PROGMEM               = "[ETH] ";
const char Connectivity::WIFI_PREFIX[] PROGMEM              = "[WIFI] ";
const char Connectivity::NTP_PREFIX[] PROGMEM               = "[NTP] ";
const char Connectivity::JSON_PREFIX[] PROGMEM              = "[JSON] ";
const char Connectivity::TCP_PREFIX[] PROGMEM               = "[TCP] ";
const char Connectivity::MQTT_PREFIX[] PROGMEM              = "[MQTT] ";
const char Connectivity::RUN_PREFIX[] PROGMEM               = "[RUN] ";

const char Connectivity::WL_NO_SHIELD_STR[] PROGMEM                 = "WL_NO_SHIELD";
const char Connectivity::WL_IDLE_STATUS_STR[] PROGMEM               = "WL_IDLE_STATUS";
const char Connectivity::WL_NO_SSID_AVAIL_STR[] PROGMEM             = "WL_NO_SSID_AVAIL";
const char Connectivity::WL_SCAN_COMPLETED_STR[] PROGMEM            = "WL_SCAN_COMPLETED";
const char Connectivity::WL_CONNECTED_STR[] PROGMEM                 = "WL_CONNECTED";
const char Connectivity::WL_CONNECT_FAILED_STR[] PROGMEM            = "WL_CONNECT_FAILED";
const char Connectivity::WL_CONNECTION_LOST_STR[] PROGMEM           = "WL_CONNECTION_LOST";
const char Connectivity::WL_WRONG_PASSWORD_STR[] PROGMEM            = "WL_WRONG_PASSWORD";
const char Connectivity::WL_DISCONNECTED_STR[] PROGMEM              = "WL_DISCONNECTED";
const char Connectivity::WL_UNKNOWN_STATUS_STR[] PROGMEM            = "WL_UNKNOWN_STATUS";
const char Connectivity::MQTT_CONNECTION_TIMEOUT_STR[] PROGMEM      = "MQTT_CONNECTION_TIMEOUT";
const char Connectivity::MQTT_CONNECTION_LOST_STR[] PROGMEM         = "MQTT_CONNECTION_LOST";
const char Connectivity::MQTT_CONNECT_FAILED_STR[] PROGMEM          = "MQTT_CONNECT_FAILED";
const char Connectivity::MQTT_DISCONNECTED_STR[] PROGMEM            = "MQTT_DISCONNECTED";
const char Connectivity::MQTT_CONNECTED_STR[] PROGMEM               = "MQTT_CONNECTED";
const char Connectivity::MQTT_CONNECT_BAD_PROTOCOL_STR[] PROGMEM    = "MQTT_CONNECT_BAD_PROTOCOL";
const char Connectivity::MQTT_CONNECT_BAD_CLIENT_ID_STR[] PROGMEM   = "MQTT_CONNECT_BAD_CLIENT_ID";
const char Connectivity::MQTT_CONNECT_UNAVAILABLE_STR[] PROGMEM     = "MQTT_CONNECT_UNAVAILABLE";
const char Connectivity::MQTT_CONNECT_BAD_CREDENTIALS_STR[] PROGMEM = "MQTT_CONNECT_BAD_CREDENTIALS";
const char Connectivity::MQTT_CONNECT_UNAUTHORIZED_STR[] PROGMEM    = "MQTT_CONNECT_UNAUTHORIZED";
const char Connectivity::MQTT_UNKNOWN_STATUS_STR[] PROGMEM          = "MQTT_UNKNOWN_STATUS";

#ifdef ESP8266
Connectivity::Connectivity(HardwareSerial& serial, DebugLedHandler& debugLed, void (*resetWdt)(), uint8_t ethCS) :
  ethInt(ethCS),
#elif defined ESP32
Connectivity::Connectivity(HardwareSerial& serial, DebugLedHandler& debugLed, void (*resetWdt)()) :
#endif
  serialPort(serial),
  debugLed(debugLed),
  tcpClient(),
  mqttClient(tcpClient),
  usedInterface(Interface::UNKNOWN),
  interfaceStatus(WL_CONNECTED),
  mqttState(MQTT_CONNECTED),
  deviceResetTimer(0U),
  resetWdt(resetWdt),
  common(*this, "common")
{}

void Connectivity::begin(Interface interface, bool errorHandling) {
  const uint32_t conTime = millis();
  const bool conResult = beginSimple(interface);
  serialPort.printf_P(PSTR("%sIOT connection: %s\r\n"), INIT_PREFIX, Str::getStateStr(conResult));
  serialPort.printf_P(PSTR("%sInit time was: %lums\r\n"), INIT_PREFIX, (millis() - conTime));
  if(!conResult && errorHandling) { ResetHandler::restartMCU(); }
}

bool Connectivity::beginSimple(Interface interface) {
  const char loadingMark = '.';
  debugLed.startTicker(500U);
  serialPort.printf_P(PSTR("%sCPP: %u\r\n"), INIT_PREFIX, Build::getCppVersion());
  serialPort.printf_P(PSTR("%sFW: %hu\r\n"), INIT_PREFIX, Build::getFwVersion());
  serialPort.printf_P(PSTR("%sGIT: %x\r\n"), INIT_PREFIX, Build::getGitHash());
  serialPort.printf_P(PSTR("%sDirty: %hu\r\n"), INIT_PREFIX, Build::getGitDirty());
#ifdef ESP8266
  serialPort.printf_P(PSTR("%sInternal VCC: %humV\r\n"), INIT_PREFIX, ESP.getVcc());
#endif
  serialPort.printf_P(PSTR("%sBegin connection...\r\n"), INIT_PREFIX);
  serialPort.flush();

  // Init filesystem.
  {
    delay(10);
    const bool initFS = LittleFS.begin();
    serialPort.printf_P(PSTR("%sInitialising filesystem: %s\r\n"), FS_PREFIX, Str::getStateStr(initFS));
    if(!initFS) { return false; }
#ifdef ESP8266
    {
      FSInfo fsInfo;
      LittleFS.info(fsInfo);
      serialPort.printf_P(PSTR("  Total bytes: %u\r\n  Used bytes: %u\r\n  Free bytes: %u\r\n  Block size: %u\r\n  Page size: %u\r\n  Max open files: %u\r\n  Max path lengths: %u\r\n"),
        fsInfo.totalBytes, fsInfo.usedBytes, (fsInfo.totalBytes - fsInfo.usedBytes), fsInfo.blockSize, fsInfo.pageSize, fsInfo.maxOpenFiles, fsInfo.maxPathLength);
    }
#elif defined ESP32
    serialPort.printf_P(PSTR("  Total bytes: %u\r\n  Used bytes: %u\r\n  Free bytes: %u\r\n"),
      LittleFS.totalBytes(), LittleFS.usedBytes(), (LittleFS.totalBytes() - LittleFS.usedBytes()));
#endif
  }

  // Get MAC.
  uint8_t mac[6] = { 0 };
#ifdef ESP8266
  wifi_get_macaddr(STATION_IF, mac);
#endif

  // Start interface.
  resetWatchdogTimer();
  if(interface == Interface::ETHERNET) {
    WiFi.mode(WIFI_OFF);
#ifdef ESP8266
    ethInt.setDefault();         // default route set through this interface
    const bool ethInit = ethInt.begin(mac);
#elif defined ESP32
    WiFi.onEvent(Connectivity::WiFiEvent);
    const bool ethInit = ETH.begin(ETH_PHY_ADDR_, ETH_PHY_POWER_, ETH_PHY_MDC_, ETH_PHY_MDIO_, ETH_PHY_TYPE_, ETH_CLK_MODE_);
#endif
    serialPort.printf_P(PSTR("%sInitialising ethernet modul: %s\r\n"), ETH_PREFIX, Str::getStateStr(ethInit));
    if(!ethInit) { return false; }
    serialPort.printf_P(PSTR("%sConnecting to router"), ETH_PREFIX);
#ifdef ESP8266
    while(!ethInt.connected()) {
#elif defined ESP32
    while(!ethConnected) {    // Wait until the device receives an IP address.
#endif
      yield();
      serialPort.print(loadingMark);
      delay(300);
    }
#ifdef ESP8266
    serialPort.printf_P(PSTR(" %s\r\n"), Str::getStateStr(ethInt.connected()));
    if(!ethInt.connected()) { return false; }
    serialPort.printf_P(PSTR("  IP: %s\r\n"), ethInt.localIP().toString().c_str());
    serialPort.printf_P(PSTR("  GW: %s\r\n"), ethInt.gatewayIP().toString().c_str());
    serialPort.printf_P(PSTR("  SNM: %s\r\n"), ethInt.subnetMask().toString().c_str());
#elif defined ESP32
    serialPort.printf_P(PSTR(" %s\r\n"), Str::getStateStr(ethConnected));
    if(!ethConnected) { return false; }
    serialPort.printf_P(PSTR("  IP: %s\r\n"), ETH.localIP().toString().c_str());
    serialPort.printf_P(PSTR("  GW: %s\r\n"), ETH.gatewayIP().toString().c_str());
    serialPort.printf_P(PSTR("  SNM: %s\r\n"), ETH.subnetMask().toString().c_str());
#endif
  }
  else if(interface == Interface::WIFI) {
    const bool wifiInit = WiFi.mode(WIFI_STA);
    serialPort.printf_P(PSTR("%sInitialising wifi: %s\r\n"), WIFI_PREFIX, Str::getStateStr(wifiInit));
    if(!wifiInit) { return false; }
    WiFi.setAutoReconnect(true);
    if(!startWifi()) { return false; }
    serialPort.printf_P(PSTR("%sConnecting to router"), WIFI_PREFIX);
    while(WiFi.status() != WL_CONNECTED) {
      yield();
      serialPort.print(loadingMark);
      delay(300);
    }
    serialPort.printf_P(PSTR(" %s\r\n"), Str::getStateStr(WiFi.status() == WL_CONNECTED));
    if(WiFi.status() != WL_CONNECTED) { return false; }
    serialPort.printf_P(PSTR("  IP: %s\r\n"), WiFi.localIP().toString().c_str());
    serialPort.printf_P(PSTR("  GW: %s\r\n"), WiFi.gatewayIP().toString().c_str());
    serialPort.printf_P(PSTR("  SNM: %s\r\n"), WiFi.subnetMask().toString().c_str());
  }
  else {
    return false;
  }
#ifdef ESP32
  ETH.setHostname(BUILD_ENV_NAME);
  ETH.macAddress(mac);
#endif
  serialPort.printf_P(PSTR("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  usedInterface = interface;
  char macAddress[macStringSize] = { '\0' };
  {
    const int32_t macAddressSize = snprintf(macAddress, sizeof(macAddress), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const bool macValid = (macAddressSize >= 0 && macAddressSize < static_cast<int32_t>(sizeof(macAddress)));
    serialPort.printf_P(PSTR("%sMake string from MAC: %s\r\n"), INIT_PREFIX, Str::getStateStr(macValid));
    if(!macValid) { return false; }
  }

  // Set time via NTP, as required for x.509 validation.
  yield();
  resetWatchdogTimer();
  {
    serialPort.printf_P(PSTR("%sWaiting for NTP time sync"), NTP_PREFIX);
    configTime(0, 0, "0.hu.pool.ntp.org", "1.hu.pool.ntp.org", "2.hu.pool.ntp.org");
    time_t nowSecs = time(nullptr);
    while(nowSecs < 8 * 3600 * 2) {
      delay(300);
      serialPort.print(loadingMark);
      nowSecs = time(nullptr);
    }
    serialPort.printf_P(PSTR("\r\n%sUTC ISO time: %s\r\n"), NTP_PREFIX, getISODateTime());
  }

  // Setup MQTT topics.
  {
    memccpy_P(mqttCredentials.userName, mqttSettings::userName, '\0', sizeof(mqttCredentials.userName));
    memccpy_P(mqttCredentials.password, mqttSettings::password, '\0', sizeof(mqttCredentials.password));
    memccpy_P(mqttCredentials.serverName, mqttSettings::serverName, '\0', sizeof(mqttCredentials.serverName));
    mqttCredentials.serverPort = mqttSettings::serverPort;
    const char* deviceID = strchr(Build::getPioEnv(), '_') + 1;
    const int32_t clientNameSize = snprintf_P(mqttCredentials.clientName, sizeof(mqttCredentials.clientName), "%s_%s", deviceID, macAddress);
    const int32_t senderTopicSize = snprintf_P(mqttCredentials.senderTopic, sizeof(mqttCredentials.senderTopic), "%s/%s/%s", BASE_TOPIC, SENDER_TOPIC, macAddress);
    const int32_t receiverTopicSize = snprintf_P(mqttCredentials.receiverTopic, sizeof(mqttCredentials.receiverTopic), "%s/%s/%s/#", BASE_TOPIC, RECEIVER_TOPIC, macAddress);
    const bool clientNameValid = (clientNameSize >= 0 && clientNameSize < static_cast<int32_t>(sizeof(mqttCredentials.clientName)));
    const bool senderTopicValid = (senderTopicSize >= 0 && senderTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.senderTopic)));
    const bool receiverTopicValid = (receiverTopicSize >= 0 && receiverTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.receiverTopic)));
    serialPort.printf_P(PSTR("%sClient name: %s\r\n"), MQTT_PREFIX, Str::getStateStr(clientNameValid));
    serialPort.printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.clientName, clientNameSize);
    serialPort.printf_P(PSTR("%sSender topic: %s\r\n"), MQTT_PREFIX, Str::getStateStr(senderTopicValid));
    serialPort.printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.senderTopic, senderTopicSize);
    serialPort.printf_P(PSTR("%sReceiver topic: %s\r\n"), MQTT_PREFIX, Str::getStateStr(receiverTopicValid));
    serialPort.printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.receiverTopic, receiverTopicSize);
    serialPort.flush();
    if(!clientNameValid) { return false; }
    if(!senderTopicValid) { return false; }
    if(!receiverTopicValid) { return false; }
  }

  if(!connect()) { return false; }
  mqttClient.setCallback([this](const char* topic, uint8_t* payload, uint32_t length) { receiveMqttMessage(topic, payload, length); });

  {
    char versionString[80];
#ifdef ESP8266
    const int32_t versionStringSize = snprintf_P(versionString, sizeof(versionString), PSTR("{""\"CPP\":%u,\"FW\":%hu,\"GH\":\"%x\",\"Dirty\":%hu,\"VCC\":%hu""}"),
      Build::getCppVersion(), Build::getFwVersion(), Build::getGitHash(), Build::getGitDirty(), ESP.getVcc());
#elif defined ESP32
    const int32_t versionStringSize = snprintf_P(versionString, sizeof(versionString), PSTR("{""\"CPP\":%u,\"FW\":%hu,\"GH\":\"%x\",\"Dirty\":%hu""}"),
      Build::getCppVersion(), Build::getFwVersion(), Build::getGitHash(), Build::getGitDirty());
#endif
    const bool versionStringValid = (versionStringSize >= 0 && versionStringSize < static_cast<int32_t>(sizeof(versionString)));
    if(!versionStringValid) { return false; }
    common.messageSend(versionString);
  }

  serialPort.printf_P(PSTR("%sInit registered objects:\r\n"), INIT_PREFIX);
  for(std::size_t i = 0; i < messageMap.size(); ++i) {
    const auto& currentObject = messageMap[i];
    if(currentObject != nullptr) {
      const bool beginResult = currentObject->begin();
      serialPort.printf_P(PSTR("  %zu. %s -> %s\r\n"), i, currentObject->getClassId(), Str::getStateStr(beginResult));
    }
    else {
      serialPort.printf_P(PSTR("  %zu. No object here!\r\n"), i);
    }
  }

  debugLed.stopTicker();
  return true;
}

bool Connectivity::startWifi() {
  char ssid[ConfigHandler::getMaxWifiSsidSize()] = {'\0'};
  char password[ConfigHandler::getMaxWifiPasswordSize()] = {'\0'};
  const uint8_t wifiConfigResult = ConfigHandler::getWifiConfig(ssid, password);
  const bool wifiConfigOk = (wifiConfigResult == 0U);
  serialPort.printf_P(PSTR("%sWifi config: %s\r\n"), WIFI_PREFIX, Str::getStateStr(wifiConfigOk));
  if(!wifiConfigOk) {
    serialPort.printf_P(PSTR("Code: %hu\r\n"), wifiConfigResult);
  } else {
    WiFi.begin(ssid, password);
  }
  return wifiConfigOk;
}

bool Connectivity::connect() {
  yield();
#ifdef ESP8266
  // Open cert.
  X509List cert(mqttSettings::caCert);
  tcpClient.setTrustAnchors(&cert);
  tcpClient.setTimeout(5000);

  // TCP connection.
  //tcpClient.getLastSSLError() == BR_ERR_OK ?
  const bool tcpStopResult = tcpClient.stop(2000);
  serialPort.printf_P(PSTR("%sReset connection for fresh start %s\r\n"), TCP_PREFIX, Str::getStateStr(tcpStopResult));
  if(!tcpStopResult) { return false; }
#elif defined ESP32
  //tcpClient.stop();
  tcpClient.setCACert(mqttSettings::caCert);
  tcpClient.setTimeout(10);
#endif
  const bool tcpConResult = tcpClient.connect(mqttCredentials.serverName, mqttCredentials.serverPort);
  serialPort.printf_P(PSTR("%sConnecting to: %s:%hu %s\r\n"), TCP_PREFIX, mqttCredentials.serverName, mqttCredentials.serverPort, Str::getStateStr(tcpConResult));
  if(!tcpConResult) { return false; }

  // MQTT connection.
  mqttClient.setServer(mqttCredentials.serverName, mqttCredentials.serverPort);
  const bool mqttConResult = mqttClient.connect(mqttCredentials.clientName, mqttCredentials.userName, mqttCredentials.password);
  serialPort.printf_P(PSTR("%sConnecting to MQTT broker: %s\r\n  State: %s\r\n"), MQTT_PREFIX, Str::getStateStr(mqttConResult), getMqttStatusStr(mqttClient.state()));
  if(!mqttConResult) { return false; }
  const bool subResult = mqttClient.subscribe(mqttCredentials.receiverTopic, 1);
  serialPort.printf_P(PSTR("%sSubscription: %s\r\n"), MQTT_PREFIX, Str::getStateStr(subResult));
  if(!subResult) { return false; }
  return true;
}

void Connectivity::loop() {
  const bool loopingResult = loopSimple();
  const bool statusChanged = loopingResult != isDeviceOnline;
  const uint32_t actualTime = millis();
  if(loopingResult) {
    deviceResetTimer = actualTime;
  }
  if(statusChanged) {
    isDeviceOnline = loopingResult;
    if(isDeviceOnline) {
      debugLed.stopTicker();
    } else {
      debugLed.startTicker(250U);
    }
    serialPort.printf_P(PSTR("%sDevice is: %s\r\n"), RUN_PREFIX, reinterpret_cast<const char*>(isDeviceOnline ? F("ONLINE") : F("OFFLINE")));
  }
  if(Time::hasElapsed(actualTime, deviceResetTimer, deviceResetTime)) {
    serialPort.printf_P(PSTR("%sDevice is offline since: %ums\r\n"), RUN_PREFIX, (actualTime - deviceResetTimer));
    ResetHandler::restartMCU();
  }
}

bool Connectivity::loopSimple() {
  yield();
  static wl_status_t actualInterfaceStatus = WL_DISCONNECTED;
  const char* intPrefix;
  if(usedInterface == Interface::ETHERNET) {
#ifdef ESP8266
    actualInterfaceStatus = ethInt.status();
#elif defined ESP32
    actualInterfaceStatus = ethConnected ? WL_CONNECTED : WL_DISCONNECTED;
#endif
    intPrefix = ETH_PREFIX;
  }
  else if(usedInterface == Interface::WIFI) { actualInterfaceStatus = WiFi.status();  intPrefix = WIFI_PREFIX; }
  else { return false; }
  if(interfaceStatus != actualInterfaceStatus) {
    serialPort.printf_P(PSTR("%sStatus changed: %s -> %s\r\n"), intPrefix, getIntStatusStr(interfaceStatus), getIntStatusStr(actualInterfaceStatus));
    interfaceStatus = actualInterfaceStatus;
    if(interfaceStatus == WL_CONNECTED) { connect(); }
    else { mqttClient.disconnect(); }
  }

  const int8_t actualMqttState = mqttClient.state();
  if(mqttState != actualMqttState) {
    serialPort.printf_P(PSTR("%sStatus changed: %s -> %s\r\n"), MQTT_PREFIX, getMqttStatusStr(mqttState), getMqttStatusStr(actualMqttState));
    mqttState = actualMqttState;
  }

  if(!mqttClient.loop()) {
    if((mqttState < MQTT_CONNECTED) && (interfaceStatus == WL_CONNECTED)) {
      static uint32_t reconnectTimer = millis();
      if(millis() - reconnectTimer >= 10000) {
        reconnectTimer = millis();
        connect();
      }
    }
  }

  for(const auto &currentObject : messageMap) {
    if(currentObject != nullptr) {
      currentObject->loop();
    }
  }

  return ((interfaceStatus == WL_CONNECTED) && (mqttState == MQTT_CONNECTED));
}

bool Connectivity::getConnectionState() { return isDeviceOnline; }

void Connectivity::receiveMqttMessage(const char* topic, uint8_t* payload, uint32_t length) {
  const char* classID = strrchr(topic, '/') + 1;
  if(!classID) { return; }
  for(const auto &currentObject : messageMap) {
    if(currentObject == nullptr) { return; }
    if(strcmp(currentObject->getClassId(), classID) == 0) {
      currentObject->messageReceived(payload, length);
      return;
    }
  }
  serialPort.printf_P(PSTR("%sNo handler -> %s\r\n"), MQTT_PREFIX, classID);
}

void Connectivity::sendMqttMessage(const char* subTopic, const char* payload) {
  char actualTopic[sizeof(mqttCredentials.senderTopic)];
  const int32_t actualTopicSize = snprintf_P(actualTopic, sizeof(actualTopic), "%s/%s", mqttCredentials.senderTopic, subTopic);
  const bool actualTopicValid = (actualTopicSize >= 0 && actualTopicSize < static_cast<int32_t>(sizeof(actualTopic)));
  if(!actualTopicValid) { return; }
  mqttClient.publish(actualTopic, payload);
}

const char* Connectivity::getISODateTime() {
  const time_t time_ = time(nullptr);
  static char buffer[24];
  memset(buffer, '\0', sizeof(buffer));
  struct tm * timeinfo;
  timeinfo = gmtime(&time_); // Convert time to UTC time structure
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", timeinfo); // Format as ISO UTC string
  return static_cast<const char*>(buffer);
}

bool Connectivity::registerCallback(Connectivity::MqttComBase* obj) {
  if(!obj) { return false; }
  messageMap.push_back(obj);
  return true;
}

const char* Connectivity::getIntStatusStr(wl_status_t status) {
  switch(status) {
    case WL_NO_SHIELD: { return WL_NO_SHIELD_STR; } break;
    case WL_IDLE_STATUS: { return WL_IDLE_STATUS_STR; } break;
    case WL_NO_SSID_AVAIL: { return WL_NO_SSID_AVAIL_STR; } break;
    case WL_SCAN_COMPLETED: { return WL_SCAN_COMPLETED_STR; } break;
    case WL_CONNECTED: { return WL_CONNECTED_STR; } break;
    case WL_CONNECT_FAILED: { return WL_CONNECT_FAILED_STR; } break;
    case WL_CONNECTION_LOST: { return WL_CONNECTION_LOST_STR; } break;
#ifdef ESP8266
    case WL_WRONG_PASSWORD: { return WL_WRONG_PASSWORD_STR; } break;
#endif
    case WL_DISCONNECTED: { return WL_DISCONNECTED_STR; } break;
    default: { return WL_UNKNOWN_STATUS_STR; } break;
  }
}

const char* Connectivity::getMqttStatusStr(int8_t status) {
  switch(status) {
    case MQTT_CONNECTION_TIMEOUT: { return MQTT_CONNECTION_TIMEOUT_STR; } break;
    case MQTT_CONNECTION_LOST: { return MQTT_CONNECTION_LOST_STR; } break;
    case MQTT_CONNECT_FAILED: { return MQTT_CONNECT_FAILED_STR; } break;
    case MQTT_DISCONNECTED: { return MQTT_DISCONNECTED_STR; } break;
    case MQTT_CONNECTED: { return MQTT_CONNECTED_STR; } break;
    case MQTT_CONNECT_BAD_PROTOCOL: { return MQTT_CONNECT_BAD_PROTOCOL_STR; } break;
    case MQTT_CONNECT_BAD_CLIENT_ID: { return MQTT_CONNECT_BAD_CLIENT_ID_STR; } break;
    case MQTT_CONNECT_UNAVAILABLE: { return MQTT_CONNECT_UNAVAILABLE_STR; } break;
    case MQTT_CONNECT_BAD_CREDENTIALS: { return MQTT_CONNECT_BAD_CREDENTIALS_STR; } break;
    case MQTT_CONNECT_UNAUTHORIZED: { return MQTT_CONNECT_UNAUTHORIZED_STR; } break;
    default: { return MQTT_UNKNOWN_STATUS_STR; } break;
  }
}

#ifdef ESP32
  void Connectivity::WiFiEvent(WiFiEvent_t event) {
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

//////////////////// -- MqttComBase class-- ////////////////////

Connectivity::MqttComBase::MqttComBase(Connectivity& connectivity, const char* classID) : conn(connectivity) {
  strlcpy(this->classId, classID, sizeof(this->classId));
  conn.registerCallback(this);
}

void Connectivity::MqttComBase::messageSend(const char* payload) const {
  conn.sendMqttMessage(getClassId(), payload);
}

const char* Connectivity::MqttComBase::getIsoTime() { return conn.getISODateTime(); }

bool Connectivity::MqttComBase::sendResponse(Response resp, uint16_t cmd) {
  static constexpr const uint8_t respBufSize = 28;
  char respBuf[respBufSize] = { '\0' };
  const int32_t respBufRealSize = snprintf_P(respBuf, sizeof(respBuf), PSTR("{""\"type\":%hu,""\"cmd\":%hu""}"), static_cast<uint8_t>(resp), cmd);
  const bool respBufValid = (respBufRealSize >= 0 && respBufRealSize < static_cast<int32_t>(sizeof(respBuf)));
  if(!respBufValid) { return false; }
  messageSend(respBuf);
  return true;
}

const char* Connectivity::MqttComBase::getClassId() const { return classId; }

//////////////////// -- Common class-- ////////////////////

const char Connectivity::Common::COMMON_PREFIX[] PROGMEM              = "[COMMON]";

Connectivity::Common::Common(Connectivity& connectivity, const char* classID) :
  MqttComBase(connectivity, classID),
  externalFileName{'\0'},
  dataTransfer(conn.serialPort)
{}

void Connectivity::Common::messageReceived(uint8_t* payload, uint32_t length) {
  JsonDocument cmdJson;
  DeserializationError deserializationError = deserializeJson(cmdJson, payload, length);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(!deSerResult) {
    conn.serialPort.printf_P(PSTR("%s Deserialisation failed: %s\r\n"), COMMON_PREFIX, reinterpret_cast<const char*>(deserializationError.f_str()));
    return;
  }
  const uint8_t cmd = cmdJson[F("cmd")].as<uint8_t>();
  Command command = static_cast<Command>(cmd);
  switch(command) {
    case Command::BLANK: {} break;
    case Command::RESTART: { ResetHandler::restartMCU(); } break;
    case Command::FW_DT_START:
    case Command::WIFICFG_DT_START:
    case Command::EXT_FILE_DT_START: {
      const uint32_t fileSize = cmdJson[F("fileSize")].as<uint32_t>();
      const uint32_t fileCrc = cmdJson[F("crc32")].as<uint32_t>();
      const char* fileNamePtr = nullptr;
      switch(command) {
        case Command::FW_DT_START: {
          const char* binId = cmdJson[F("binId")].as<const char*>();
          if(strncmp_P(binId, Build::getPioEnv(), Build::getPioEnvLength()) != 0) {
            conn.serialPort.printf_P(PSTR("%s Wrong FW file ID: %s\r\n"), COMMON_PREFIX, binId);
            MqttComBase::sendResponse(MqttComBase::Response::NACK, cmd);
            return;
          }
          fileNamePtr = FileName::getOtaFwLocation();
          } break;
        case Command::WIFICFG_DT_START: { fileNamePtr = FileName::getWifiTempConfigLocation(); } break;
        case Command::EXT_FILE_DT_START: {
          const char* fileName = cmdJson[F("name")].as<const char*>();
          memset(externalFileName, '\0', sizeof(externalFileName));
          if(fileName != nullptr) { memccpy(externalFileName, fileName, '\0', sizeof(externalFileName)); }
          uint32_t externalFileNameSize =  strnlen(externalFileName, sizeof(externalFileName));
          if(externalFileNameSize == 0 || externalFileNameSize >= sizeof(externalFileName)) {
            conn.serialPort.printf_P(PSTR("%s Wrong file name: missing / too long!\r\n"), COMMON_PREFIX);
            MqttComBase::sendResponse(MqttComBase::Response::NACK, cmd);
            return;
          }
          fileNamePtr = static_cast<const char*>(externalFileName);
        } break;
        default: {} break;
      }
      const bool transferBeginResult = dataTransfer.begin(fileSize, fileCrc, fileNamePtr);
      MqttComBase::sendResponse((transferBeginResult ? MqttComBase::Response::ACK : MqttComBase::Response::NACK), cmd);
      if(!transferBeginResult) {
        conn.serialPort.printf_P(PSTR("%s Can't begin file transfer:\r\n  Name: %s\r\n"), COMMON_PREFIX, fileNamePtr);
        return;
      }
    } break;
    case Command::FW_DT_DATA:
    case Command::WIFICFG_DT_DATA:
    case Command::EXT_FILE_DT_DATA: {
      const uint32_t filePieceNumber = cmdJson[F("piece")].as<uint32_t>();
      const char* filePieceB64 = cmdJson["data"].as<const char*>();
      const bool storingResult = dataTransfer.storeBase64(filePieceNumber, filePieceB64);
      MqttComBase::sendResponse(storingResult ? MqttComBase::Response::ACK : MqttComBase::Response::NACK, cmd);
      if(!storingResult) {
        conn.serialPort.printf_P(PSTR("%s File storing failed!\r\n"), COMMON_PREFIX);
      }
    } break;
    case Command::FW_DT_END:
    case Command::WIFICFG_DT_END:
    case Command::EXT_FILE_DT_END: {
      const bool validityCheckResult = dataTransfer.checkValidity();
      MqttComBase::sendResponse((validityCheckResult ? MqttComBase::Response::ACK : MqttComBase::Response::NACK), cmd);
      if(!validityCheckResult) {
        conn.serialPort.printf_P(PSTR("%s Stored file is not valid!\r\n"), COMMON_PREFIX);
        return;
      }
      if(command == Command::FW_DT_END) {
        const bool fwUpdatePreparationOk = dataTransfer.upgradeFirmware();
        if(!fwUpdatePreparationOk) {
          conn.serialPort.printf_P(PSTR("%s FW upgrade preparation failed!\r\n"), COMMON_PREFIX);
          return;
        }
        ResetHandler::restartMCU();
      }
    } break;
  };
}

bool Connectivity::Common::begin() { return true; }

bool Connectivity::Common::loop() { return true; }

void Connectivity::Common::messageSend(const char* payload) const {
  MqttComBase::messageSend(payload);
}