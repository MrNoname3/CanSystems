#include "connectivity.hpp"
#include "wdtHandler.hpp"                                           /// Handles the watchdog timer.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include <LittleFS.h>                                               /// Use FLASH filesystem.
#include <ArduinoJson.h>                                            /// Handle JSON files.
#ifdef ESP8266
#include <Updater.h>
#elif defined ESP32
#include <Update.h>
#include <esp_task_wdt.h>
#endif
#include "crc32.hpp"
#include "base64.hpp"

#ifdef ESP8266
// Monitor the internal VCC level, it varies with WiFi load.
// Don't connect anything to the analog input pin!
ADC_MODE(ADC_VCC);
#elif defined ESP32
bool Connectivity::ethConnected = false;
#endif
bool Connectivity::isDeviceOnline = true;

const char Connectivity::wifiFileLocation[] PROGMEM         = "/config/wifi.json";
const char Connectivity::BASE_TOPIC[] PROGMEM               = "iot";
const char Connectivity::SENDER_TOPIC[] PROGMEM             = "dtos";
const char Connectivity::RECEIVER_TOPIC[] PROGMEM           = "stod";
const char Connectivity::OK_STATE[] PROGMEM                 = " [OK]";                    // OK status.
const char Connectivity::ERR_STATE[] PROGMEM                = " [ERR]";                   // Error status.
const char Connectivity::DEVICE_TYPE[] PROGMEM              = BUILD_ENV_NAME;
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
Connectivity::Connectivity(HardwareSerial& serial, DebugLedHandler& debugLed, uint8_t ethCS) :
  ethInt(ethCS),
#elif defined ESP32
Connectivity::Connectivity(HardwareSerial& serial, DebugLedHandler& debugLed) :
#endif
  serialPort(serial),
  debugLed(debugLed),
  tcpClient(),
  mqttClient(tcpClient),
  usedInterface(Interface::UNKNOWN),
  interfaceStatus(WL_CONNECTED),
  mqttState(MQTT_CONNECTED),
  cppVersion(__cplusplus),
  fwVersion(GIT_COMMIT_COUNT),
  gitHash(GIT_COMMIT_HASH),
  timeTracker(deviceResetTime),
  loopTimeTracker(1),
  dataTransfer(&serialPort),
  common(*this, "common")
{}



void Connectivity::begin(Interface interface, bool errorHandling) {
  WdtHandler::enableWatchdog();
  TimeTracker conTime;
  conTime.startTime();
  const bool conResult = beginSimple(interface);
  serialPort.printf_P(PSTR("%sIOT connection:%s\r\n"), INIT_PREFIX, (conResult ? OK_STATE : ERR_STATE));
  serialPort.printf_P(PSTR("%sInit time was: %ums\r\n"), INIT_PREFIX, conTime.stopTime());
  if(!conResult && errorHandling) { ResetHandler::restartMCU(); }
}

bool Connectivity::beginSimple(Interface interface) {
  const char loadingMark = '.';
  debugLed.startTicker(500U);
  serialPort.printf_P(PSTR("%sCPP: %u\r\n"), INIT_PREFIX, cppVersion);
  serialPort.printf_P(PSTR("%sFW: %hu\r\n"), INIT_PREFIX, fwVersion);
  serialPort.printf_P(PSTR("%sGit hash: %x\r\n"), INIT_PREFIX, gitHash);
#ifdef ESP8266
  serialPort.printf_P(PSTR("%sInternal VCC: %humV\r\n"), INIT_PREFIX, ESP.getVcc());
#endif
  serialPort.printf_P(PSTR("%sBegin connection...\r\n"), INIT_PREFIX);
  serialPort.flush();

  // Init filesystem.
  {
    delay(10);
    const bool initFS = LittleFS.begin();
    serialPort.printf_P(PSTR("%sInitialising filesystem:%s\r\n"), FS_PREFIX, (initFS ? OK_STATE : ERR_STATE));
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
  WdtHandler::resetWatchdog();
  if(interface == Interface::ETHERNET) {
    WiFi.mode(WIFI_OFF);
#ifdef ESP8266
    ethInt.setDefault();         // default route set through this interface
    const bool ethInit = ethInt.begin(mac);
#elif defined ESP32
    WiFi.onEvent(Connectivity::WiFiEvent);
    const bool ethInit = ETH.begin(ETH_PHY_ADDR_, ETH_PHY_POWER_, ETH_PHY_MDC_, ETH_PHY_MDIO_, ETH_PHY_TYPE_, ETH_CLK_MODE_);
#endif
    serialPort.printf_P(PSTR("%sInitialising ethernet modul:%s\r\n"), ETH_PREFIX, ethInit ? OK_STATE : ERR_STATE);
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
    serialPort.printf_P(PSTR("%s\r\n"), ethInt.connected() ? OK_STATE : ERR_STATE);
    if(!ethInt.connected()) { return false; }
    serialPort.printf_P(PSTR("  IP: %s\r\n"), ethInt.localIP().toString().c_str());
    serialPort.printf_P(PSTR("  GW: %s\r\n"), ethInt.gatewayIP().toString().c_str());
    serialPort.printf_P(PSTR("  SNM: %s\r\n"), ethInt.subnetMask().toString().c_str());
#elif defined ESP32
    serialPort.printf_P(PSTR("%s\r\n"), ethConnected ? OK_STATE : ERR_STATE);
    if(!ethConnected) { return false; }
    serialPort.printf_P(PSTR("  IP: %s\r\n"), ETH.localIP().toString().c_str());
    serialPort.printf_P(PSTR("  GW: %s\r\n"), ETH.gatewayIP().toString().c_str());
    serialPort.printf_P(PSTR("  SNM: %s\r\n"), ETH.subnetMask().toString().c_str());
#endif
  }
  else if(interface == Interface::WIFI) {
    const bool wifiInit = WiFi.mode(WIFI_STA);
    serialPort.printf_P(PSTR("%sInitialising wifi:%s\r\n"), WIFI_PREFIX, wifiInit ? OK_STATE : ERR_STATE);
    if(!wifiInit) { return false; }
    WiFi.setAutoReconnect(true);
    if(!startWifi()) { return false; }
    serialPort.printf_P(PSTR("%sConnecting to router"), WIFI_PREFIX);
    while(WiFi.status() != WL_CONNECTED) {
      yield();
      serialPort.print(loadingMark);
      delay(300);
    }
    serialPort.printf_P(PSTR("%s\r\n"), (WiFi.status() == WL_CONNECTED) ? OK_STATE : ERR_STATE);
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
    serialPort.printf_P(PSTR("%sMake string from MAC:%s\r\n"), INIT_PREFIX, macValid ? OK_STATE : ERR_STATE);
    if(!macValid) { return false; }
  }

  // Set time via NTP, as required for x.509 validation.
  yield();
  WdtHandler::resetWatchdog();
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
    const char* deviceID = strchr(DEVICE_TYPE, '_') + 1;
    const int32_t clientNameSize = snprintf_P(mqttCredentials.clientName, sizeof(mqttCredentials.clientName), "%s_%s", deviceID, macAddress);
    const int32_t senderTopicSize = snprintf_P(mqttCredentials.senderTopic, sizeof(mqttCredentials.senderTopic), "%s/%s/%s", BASE_TOPIC, SENDER_TOPIC, macAddress);
    const int32_t receiverTopicSize = snprintf_P(mqttCredentials.receiverTopic, sizeof(mqttCredentials.receiverTopic), "%s/%s/%s/#", BASE_TOPIC, RECEIVER_TOPIC, macAddress);
    const bool clientNameValid = (clientNameSize >= 0 && clientNameSize < static_cast<int32_t>(sizeof(mqttCredentials.clientName)));
    const bool senderTopicValid = (senderTopicSize >= 0 && senderTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.senderTopic)));
    const bool receiverTopicValid = (receiverTopicSize >= 0 && receiverTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.receiverTopic)));
    serialPort.printf_P(PSTR("%sClient name:%s\r\n"), MQTT_PREFIX, clientNameValid ? OK_STATE : ERR_STATE);
    serialPort.printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.clientName, clientNameSize);
    serialPort.printf_P(PSTR("%sSender topic:%s\r\n"), MQTT_PREFIX, senderTopicValid ? OK_STATE : ERR_STATE);
    serialPort.printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.senderTopic, senderTopicSize);
    serialPort.printf_P(PSTR("%sReceiver topic:%s\r\n"), MQTT_PREFIX, receiverTopicValid ? OK_STATE : ERR_STATE);
    serialPort.printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.receiverTopic, receiverTopicSize);
    serialPort.flush();
    if(!clientNameValid) { return false; }
    if(!senderTopicValid) { return false; }
    if(!receiverTopicValid) { return false; }
  }

  if(!connect()) { return false; }
  mqttClient.setCallback([this](const char* topic, uint8_t* payload, uint32_t length) { receiveMqttMessage(topic, payload, length); });

  {
    char versionString[64];
#ifdef ESP8266
    const int32_t versionStringSize = snprintf_P(versionString, sizeof(versionString), PSTR("{""\"CPP\":%u,\"FW\":%hu,\"GH\":\"%x\",\"VCC\":%hu""}"), cppVersion, fwVersion, gitHash, ESP.getVcc());
#elif defined ESP32
    const int32_t versionStringSize = snprintf_P(versionString, sizeof(versionString), PSTR("{""\"CPP\":%u,\"FW\":%hu,\"GH\":\"%x\"}"), cppVersion, fwVersion, gitHash);
#endif
    const bool versionStringValid = (versionStringSize >= 0 && versionStringSize < static_cast<int32_t>(sizeof(versionString)));
    if(!versionStringValid) { return false; }
    common.messageSend(versionString);
  }

  WdtHandler::resetWatchdog();
  serialPort.printf_P(PSTR("%sInit registered objects:\r\n"), INIT_PREFIX);
  for(std::size_t i = 0; i < messageMap.size(); ++i) {
    const auto& currentObject = messageMap[i];
    if(currentObject != nullptr) {
      const bool beginResult = currentObject->begin();
      serialPort.printf_P(PSTR("  %zu. %s ->%s\r\n"), i, currentObject->getClassId(), beginResult ? OK_STATE : ERR_STATE);
    }
    else {
      serialPort.printf_P(PSTR("  %zu. No object here!\r\n"), i);
    }
  }

  debugLed.stopTicker();
  WdtHandler::resetWatchdog();
  return true;
}

bool Connectivity::startWifi() {
  bool retVal = false;
  const bool wifiFileExists = LittleFS.exists(FPSTR(wifiFileLocation));
  serialPort.printf_P(PSTR("%sCheck wifi config:\r\n"), FS_PREFIX);
  serialPort.printf_P(PSTR("  %s ->%s\r\n"), wifiFileLocation, wifiFileExists ? OK_STATE : ERR_STATE);
  if(!wifiFileExists) { return retVal; }

  File wifiFile = LittleFS.open(FPSTR(wifiFileLocation), "r");
  serialPort.printf_P(PSTR("%sOpening: %s%s\r\n"), FS_PREFIX, wifiFileLocation, wifiFile ? OK_STATE : ERR_STATE);
  if(!wifiFile) { wifiFile.close(); return retVal; }

  JsonDocument wifiJson;
  DeserializationError deserializationError = deserializeJson(wifiJson, wifiFile);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(deSerResult) {
    JsonVariant ssidJsonVar = wifiJson[F("ssid")];
    JsonVariant passwordJsonVar = wifiJson[F("password")];
    if(ssidJsonVar.is<const char*>() && passwordJsonVar.is<const char*>()) {
      constexpr uint8_t maxSsidLength = 48;
      constexpr uint8_t maxPassLength = 48;
      const char* ssid = ssidJsonVar.as<const char*>();
      const char* pass = passwordJsonVar.as<const char*>();
      const uint8_t ssidLength = strnlen(ssid, maxSsidLength);
      const uint8_t passLength = strnlen(pass, maxPassLength);
      const bool ssidLengthValid = (ssidLength > 0) && (ssidLength < maxSsidLength);
      const bool passLengthValid = (passLength > 0) && (passLength < maxPassLength);
      if(ssidLengthValid && passLengthValid) {
        WiFi.begin(ssid, pass);
        retVal = true;
      }
      else {
        serialPort.printf_P(PSTR("%sWifi credentials are empty!\r\n"), JSON_PREFIX);
      }
    }
    else {
      serialPort.printf_P(PSTR("%sKeys are not presented in the file!\r\n"), JSON_PREFIX);
    }
  }
  else {
    serialPort.printf_P(PSTR("%sDeserialisation failed: %s\r\n"), JSON_PREFIX, deserializationError.f_str());
  }
  wifiFile.close();
  return retVal;
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
  serialPort.printf_P(PSTR("%sReset connection for fresh start%s\r\n"), TCP_PREFIX, tcpStopResult ? OK_STATE : ERR_STATE);
  if(!tcpStopResult) { return false; }
#elif defined ESP32
  //tcpClient.stop();
  tcpClient.setCACert(mqttSettings::caCert);
  tcpClient.setTimeout(10);
#endif
  const bool tcpConResult = tcpClient.connect(mqttCredentials.serverName, mqttCredentials.serverPort);
  serialPort.printf_P(PSTR("%sConnecting to: %s:%hu%s\r\n"), TCP_PREFIX, mqttCredentials.serverName, mqttCredentials.serverPort, tcpConResult ? OK_STATE : ERR_STATE);
  if(!tcpConResult) { return false; }

  // MQTT connection.
  mqttClient.setServer(mqttCredentials.serverName, mqttCredentials.serverPort);
  const bool mqttConResult = mqttClient.connect(mqttCredentials.clientName, mqttCredentials.userName, mqttCredentials.password);
  serialPort.printf_P(PSTR("%sConnecting to MQTT broker:%s\r\n  State: %s\r\n"), MQTT_PREFIX, mqttConResult ? OK_STATE : ERR_STATE, getMqttStatusStr(mqttClient.state()));
  if(!mqttConResult) { return false; }
  const bool subResult = mqttClient.subscribe(mqttCredentials.receiverTopic, 1);
  serialPort.printf_P(PSTR("%sSubscription:%s\r\n"), MQTT_PREFIX, subResult ? OK_STATE : ERR_STATE);
  if(!subResult) { return false; }
  return true;
}

void Connectivity::loop() {
  loopTimeTracker.startTime();
  const bool loopingResult = loopSimple();
  const bool statusChanged = loopingResult != isDeviceOnline;
  if(statusChanged) {
    isDeviceOnline = loopingResult;
    if(isDeviceOnline) {
      debugLed.stopTicker();
      timeTracker.resetTime();
    } 
    else {
      debugLed.startTicker(250U);
      timeTracker.startTime();
    }
    serialPort.printf_P(PSTR("%sDevice is: %s\r\n"), RUN_PREFIX, isDeviceOnline ? F("ONLINE") : F("OFFLINE"));
  }
  if(timeTracker.isGoalReached()) {
    serialPort.printf_P(PSTR("%sDevice is offline since: %ums\r\n"), RUN_PREFIX, timeTracker.getElapsedTime());
    ResetHandler::restartMCU();
  }
  if(loopTimeTracker.isGoalReached()) {
    const uint32_t loopTime = loopTimeTracker.stopTime();
    serialPort.printf_P(PSTR("%sMax loop time is: %ums\r\n"), RUN_PREFIX, loopTime);
    loopTimeTracker.setGoal(loopTime + 1);
  }
}

bool Connectivity::loopSimple() {
  yield();
  WdtHandler::resetWatchdog();

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

//////////////////// -- TimeTracker class-- ////////////////////

Connectivity::TimeTracker::TimeTracker(uint32_t goalTime) : startTime_(0), goalTime_(goalTime) {}

void Connectivity::TimeTracker::startTime() { startTime_ = millis(); }

void Connectivity::TimeTracker::resetTime() { startTime_ = 0; }

void Connectivity::TimeTracker::setGoal(uint32_t goalTime) { goalTime_ = goalTime; }

uint32_t Connectivity::TimeTracker::stopTime() { 
  const uint32_t stoppedTime = getElapsedTime();
  resetTime();
  return stoppedTime;
 }

uint32_t Connectivity::TimeTracker::getElapsedTime() { return (millis() - startTime_); }

bool Connectivity::TimeTracker::isGoalReached() {
  if(startTime_ == 0) { return false; }
  return (getElapsedTime() >= goalTime_);
}

//////////////////// -- DataTransfer class-- ////////////////////

const char Connectivity::DataTransfer::FILE_TRANSFER_PREFIX[] PROGMEM     = "[FT] ";
const char Connectivity::DataTransfer::otaFwLocation[] PROGMEM            = "/config/espFirmware.bin";
const char Connectivity::DataTransfer::wifiTempFileLocation[] PROGMEM     = "/config/wifi.json.tmp";

Connectivity::DataTransfer::DataTransfer(Stream* serial) :
  serialPort(serial),
  fileSize_(0),
  fileCrc_(0),
  nextFilePieceNumber_(-1),
  remainingFileSize_(0),
  fileName_(nullptr),
  fileTransferStarted_(false) {}

bool Connectivity::DataTransfer::begin(uint32_t fileSize, uint32_t fileCrc, const char* fileName) {
  if(this->fileTransferStarted_) { stop(true); }
  this->fileTransferStarted_ = true;
  this->fileSize_ = fileSize;
  this->fileCrc_ = fileCrc;
  this->nextFilePieceNumber_ = 0;
  this->remainingFileSize_ = fileSize;
  if(fileName == nullptr) { stop(true); return false; }
  this->fileName_ = fileName;
  {
#ifdef ESP8266
    FSInfo fsInfo;
    LittleFS.info(fsInfo);
    const uint32_t freeSpace = fsInfo.totalBytes - fsInfo.usedBytes;
#elif defined ESP32
    const uint32_t freeSpace = LittleFS.totalBytes() - LittleFS.usedBytes();
#endif
    const bool isEnoughFreeSpace = freeSpace > this->fileSize_;
    if(!isEnoughFreeSpace) {
      if(this->serialPort) { this->serialPort->printf_P(PSTR("%sNot enough free space!\r\n  Available: %u\r\n  Required: %u\r\n"), FILE_TRANSFER_PREFIX, freeSpace, this->fileSize_); }
      return false;
    }
  }
  const bool fileExists = LittleFS.exists(FPSTR(this->fileName_));
  if(fileExists) {
    const bool rmFileResult = LittleFS.remove(FPSTR(this->fileName_));
    if(!rmFileResult) {
      if(this->serialPort) { this->serialPort->printf_P(PSTR("%sDeleting failed: %s\r\n"), FILE_TRANSFER_PREFIX, this->fileName_); }
      stop(true);
      return false;
    }
  }
  if(this->serialPort) {
    this->serialPort->printf_P(PSTR("%sFile transfer started:\r\n  Name: %s\r\n  Size: %u\r\n  CRC32: %u\r\n"),
      FILE_TRANSFER_PREFIX, this->fileName_, this->fileSize_, this->fileCrc_);
  }
  if(fileSize == 0) { stop(true); return false; }
  return true;
}

bool Connectivity::DataTransfer::stop(bool deleteFile) {
  this->fileTransferStarted_ = false;
  this->fileSize_ = 0;
  this->fileCrc_ = 0;
  this->nextFilePieceNumber_ = -1;
  this->remainingFileSize_ = 0;

  if(this->serialPort) { this->serialPort->printf_P(PSTR("%sFile transfer stopped, cleaning up done!\r\n"), FILE_TRANSFER_PREFIX); }
  const bool fileExists = LittleFS.exists(FPSTR(this->fileName_));
  if(fileExists && deleteFile) {
    const bool rmFileResult = LittleFS.remove(FPSTR(this->fileName_));
    if(!rmFileResult) {
      if(this->serialPort) { this->serialPort->printf_P(PSTR("%sDeleting failed: %s\r\n"), FILE_TRANSFER_PREFIX, this->fileName_); }
      return false;
    }
  }
  this->fileName_ = nullptr;
  return true;
}

bool Connectivity::DataTransfer::storeBase64(uint32_t filePieceNumber, const char* fileData) {
  if(!this->fileTransferStarted_) { return false; }
  if(filePieceNumber != this->nextFilePieceNumber_) { return false; }
  if(this->remainingFileSize_ == 0) { return false; }
  constexpr uint16_t maxB64Length = receivedFilePieceSize * 4 / 3;
  const uint32_t filePieceB64Size = strnlen(fileData, maxB64Length);
  if(filePieceB64Size == 0) { return false; }

  uint8_t decodedData[receivedFilePieceSize];
  const uint32_t decodedPreSize = Base64::decodedLength(reinterpret_cast<const uint8_t*>(fileData), filePieceB64Size);
  if(decodedPreSize > sizeof(decodedData)) {
    if(this->serialPort) { this->serialPort->printf_P(PSTR("%sFile piece size error!\r\n"), FILE_TRANSFER_PREFIX); }
    return false;
  }
  const uint32_t decodedPostSize = Base64::decodeBase64(reinterpret_cast<const uint8_t*>(fileData), decodedData, filePieceB64Size);
  if(decodedPreSize != decodedPostSize) {
    if(this->serialPort) { this->serialPort->printf_P(PSTR("%sDecoded size check error!\r\n"), FILE_TRANSFER_PREFIX); }
    return false;
  }
  const bool storingResult = store(filePieceNumber, decodedData, decodedPreSize);
  if(!storingResult) {
    if(this->serialPort) { this->serialPort->printf_P(PSTR("%sFile storing failed!\r\n"), FILE_TRANSFER_PREFIX); }
  }
  return storingResult;
}

bool Connectivity::DataTransfer::store(uint32_t filePieceNumber, const uint8_t* fileData, uint16_t fileDataSize) {
  if(!this->fileTransferStarted_) { return false; }
  if(filePieceNumber != this->nextFilePieceNumber_) { return false; }
  if(fileDataSize == 0) { return false; }
  if(this->remainingFileSize_ == 0) { return false; }

  File receivedFile = LittleFS.open(FPSTR(this->fileName_), "a");
  if(!receivedFile) {
    if(this->serialPort) { this->serialPort->printf_P(PSTR("%sOpening failed: %s\r\n"), FILE_TRANSFER_PREFIX, this->fileName_); }
    receivedFile.close();
    return false;
  }
  const uint32_t writtenBytes = receivedFile.write(fileData, fileDataSize);
  receivedFile.close();
  if(writtenBytes != fileDataSize) {
    if(this->serialPort) { this->serialPort->printf_P(PSTR("%sWriting failed: %s\r\n"), FILE_TRANSFER_PREFIX, this->fileName_); }
    return false;
  }
  this->nextFilePieceNumber_++;
  this->remainingFileSize_ -= fileDataSize;
  return true;
}

bool Connectivity::DataTransfer::checkValidity() {
  if(!this->fileTransferStarted_) { return false; }
  if(this->remainingFileSize_ != 0) { return false; }
  File receivedFile = LittleFS.open(FPSTR(this->fileName_), "r");
  if(this->serialPort) {
    this->serialPort->printf_P(PSTR("%sChecking received file:%s\r\n"),
      FILE_TRANSFER_PREFIX, receivedFile ? Connectivity::OK_STATE : Connectivity::ERR_STATE);
  }
  if(!receivedFile) { receivedFile.close(); return false; }
  const bool fileSizeOk = (receivedFile.size() == this->fileSize_);
  if(this->serialPort) { this->serialPort->printf_P(PSTR("  Size ->%s\r\n"), fileSizeOk ? Connectivity::OK_STATE : Connectivity::ERR_STATE); }
  if(!fileSizeOk) { receivedFile.close(); return false; }
  Crc32 crc32;
  while(receivedFile.available() > 0) { crc32.next(receivedFile.read()); }
  const uint32_t calcFileCrc32 = crc32.get();
  const bool fileCrcOk = (calcFileCrc32 == this->fileCrc_);
  if(this->serialPort) { this->serialPort->printf_P(PSTR("  CRC ->%s\r\n"), fileCrcOk ? Connectivity::OK_STATE : Connectivity::ERR_STATE); }
  if(!fileCrcOk) { receivedFile.close(); return false; }
  if(this->fileName_ != otaFwLocation) {
    receivedFile.close();
    stop(false);
    return true;
  }

  receivedFile.seek(0, SeekSet);
  const bool updateBeginResult = Update.begin(this->fileSize_);
  if(this->serialPort) { this->serialPort->printf_P(PSTR("  Begin ->%s\r\n"), updateBeginResult ? Connectivity::OK_STATE : Connectivity::ERR_STATE); }
  if(!updateBeginResult) { receivedFile.close(); return false; }
  const bool updateStreamResult = (Update.writeStream(receivedFile) == this->fileSize_);
  if(this->serialPort) { this->serialPort->printf_P(PSTR("  Stream ->%s\r\n"), updateStreamResult ? Connectivity::OK_STATE : Connectivity::ERR_STATE); }
  if(!updateStreamResult) { receivedFile.close(); return false; }
  receivedFile.close();
  const bool updateEndResult = Update.end();
  if(this->serialPort) { this->serialPort->printf_P(PSTR("  End ->%s\r\n"), updateEndResult ? Connectivity::OK_STATE : Connectivity::ERR_STATE); }
  stop(true);
  if(!updateEndResult) { return false; }
  return true;
}

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

const char Connectivity::Common::COMMON_PREFIX[] PROGMEM              = "[COMMON] ";

Connectivity::Common::Common(Connectivity& connectivity, const char* classID) :
  MqttComBase(connectivity, classID),
  externalFileName{'\0'} {}

void Connectivity::Common::messageReceived(uint8_t* payload, uint32_t length) {
  JsonDocument cmdJson;
  DeserializationError deserializationError = deserializeJson(cmdJson, payload, length);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(!deSerResult) {
    conn.serialPort.printf_P(PSTR("%sDeserialisation failed: %s\r\n"), COMMON_PREFIX, deserializationError.f_str());
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
          if(strncmp_P(binId, DEVICE_TYPE, sizeof(DEVICE_TYPE)) != 0) {
            conn.serialPort.printf_P(PSTR("%sWrong FW file ID: %s\r\n"), COMMON_PREFIX, binId);
            MqttComBase::sendResponse(MqttComBase::Response::NACK, cmd);
            return;
          }
          fileNamePtr = DataTransfer::otaFwLocation;
          } break;
        case Command::WIFICFG_DT_START: { fileNamePtr = DataTransfer::wifiTempFileLocation; } break;
        case Command::EXT_FILE_DT_START: {
          const char* fileName = cmdJson[F("name")].as<const char*>();
          memset(externalFileName, '\0', sizeof(externalFileName));
          if(fileName != nullptr) { memccpy(externalFileName, fileName, '\0', sizeof(externalFileName)); }
          uint32_t externalFileNameSize =  strnlen(externalFileName, sizeof(externalFileName));
          if(externalFileNameSize == 0 || externalFileNameSize >= sizeof(externalFileName)) {
            conn.serialPort.printf_P(PSTR("%sWrong file name: missing / too long!\r\n"), COMMON_PREFIX);
            MqttComBase::sendResponse(MqttComBase::Response::NACK, cmd);
            return;
          }
          fileNamePtr = static_cast<const char*>(externalFileName);
        } break;
        default: {} break;
      }
      const bool transferBeginResult = conn.dataTransfer.begin(fileSize, fileCrc, fileNamePtr);
      MqttComBase::sendResponse((transferBeginResult ? MqttComBase::Response::ACK : MqttComBase::Response::NACK), cmd);
      if(!transferBeginResult) {
        conn.serialPort.printf_P(PSTR("%sCan't begin file transfer:\r\n  Name: %s\r\n"), COMMON_PREFIX, fileNamePtr);
        return;
      }
    } break;
    case Command::FW_DT_DATA:
    case Command::WIFICFG_DT_DATA:
    case Command::EXT_FILE_DT_DATA: {
      const uint32_t filePieceNumber = cmdJson[F("piece")].as<uint32_t>();
      const char* filePieceB64 = cmdJson["data"].as<const char*>();
      const bool storingResult = conn.dataTransfer.storeBase64(filePieceNumber, filePieceB64);
      MqttComBase::sendResponse(storingResult ? MqttComBase::Response::ACK : MqttComBase::Response::NACK, cmd);
      if(!storingResult) {
        conn.serialPort.printf_P(PSTR("%sFile storing failed!\r\n"), COMMON_PREFIX);
      }
    } break;
    case Command::FW_DT_END:
    case Command::WIFICFG_DT_END:
    case Command::EXT_FILE_DT_END: {
      const bool validityCheckResult = conn.dataTransfer.checkValidity();
      MqttComBase::sendResponse((validityCheckResult ? MqttComBase::Response::ACK : MqttComBase::Response::NACK), cmd);
      if(!validityCheckResult) {
        conn.serialPort.printf_P(PSTR("%sStored file is not valid!\r\n"), COMMON_PREFIX);
      }
      if(validityCheckResult && (command == Command::FW_DT_END)) { ResetHandler::restartMCU(); }
    } break;
  };
}

bool Connectivity::Common::begin() { return true; }

bool Connectivity::Common::loop() { return true; }

void Connectivity::Common::messageSend(const char* payload) const {
  MqttComBase::messageSend(payload);
}