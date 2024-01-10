#include "connectivity.hpp"
#include <LittleFS.h>                         /// Use FLASH filesystem.
#include <ArduinoJson.h>                      /// Handle JSON files.
#include <Updater.h>
#if (defined(__AVR__) || defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_SAM))
#include <avr/pgmspace.h>
#else
#include <pgmspace.h>
#endif

// Monitor the internal VCC level, it varies with WiFi load.
// Don't connect anything to the analog input pin!
ADC_MODE(ADC_VCC);

bool Connectivity::isDeviceOnline = true;
Connectivity::MqttComBase* Connectivity::messageMap[] = { nullptr };
uint8_t Connectivity::messageMapPointer = 0;

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

Connectivity::Connectivity(Stream* serial, const uint8_t ethCS, uint8_t dbgLedPin, bool dbgLedOnState) :
  serialPort(serial),
  ethInt(ethCS),
  tcpClient(),
  mqttClient(tcpClient),
  usedInterface(Interface::UNKNOWN),
  interfaceStatus(WL_CONNECTED),
  mqttState(MQTT_CONNECTED),
  cppVersion(__cplusplus),
  fwVersion(GIT_COMMIT_COUNT),
  gitHash(GIT_COMMIT_HASH),
  debugLed(dbgLedPin, dbgLedOnState),
  timeTracker(deviceResetTime),
  loopTimeTracker(1),
  common("common", serial)
{
  WdtHandler.enableHwWdt();
}

void Connectivity::begin(Interface interface, bool errorHandling) {
  TimeTracker conTime;
  conTime.startTime();
  const bool conResult = beginSimple(interface);
  if(serialPort) { serialPort->printf_P(PSTR("%sIOT connection:%s\r\n"), INIT_PREFIX, (conResult ? OK_STATE : ERR_STATE)); }
  if(serialPort) { serialPort->printf_P(PSTR("%sInit time was: %ums\r\n"), INIT_PREFIX, conTime.stopTime()); }
  if(!conResult && errorHandling) { common.restartESP(); }
}

bool Connectivity::beginSimple(Interface interface) {
  const char loadingMark = '.';
  WdtHandler.setEnabledResetNumber(4);
  debugLed.startTicker(500);
  if(serialPort) {
    serialPort->printf_P(PSTR("%sCPP: %u\r\n"), INIT_PREFIX, cppVersion);
    serialPort->printf_P(PSTR("%sFW: %hu\r\n"), INIT_PREFIX, fwVersion);
    serialPort->printf_P(PSTR("%sGit hash: %x\r\n"), INIT_PREFIX, gitHash);
    serialPort->printf_P(PSTR("%sInternal VCC: %humV\r\n"), INIT_PREFIX, ESP.getVcc());
    serialPort->printf_P(PSTR("%sBegin connection...\r\n"), INIT_PREFIX);
    serialPort->flush();
  }

  // Init filesystem.
  {
    const bool initFS = LittleFS.begin();
    if(serialPort) { serialPort->printf_P(PSTR("%sInitialising filesystem:%s\r\n"), FS_PREFIX, (initFS ? OK_STATE : ERR_STATE)); }
    if(!initFS) { return false; }
    else {
      FSInfo fsInfo;
      LittleFS.info(fsInfo);
      if(serialPort) {
        serialPort->printf_P(PSTR("  Total bytes: %u\r\n  Used bytes: %u\r\n  Free bytes: %u\r\n  Block size: %u\r\n  Page size: %u\r\n  Max open files: %u\r\n  Max path lengths: %u\r\n"),
        fsInfo.totalBytes, fsInfo.usedBytes, (fsInfo.totalBytes - fsInfo.usedBytes), fsInfo.blockSize, fsInfo.pageSize, fsInfo.maxOpenFiles, fsInfo.maxPathLength);
      }
    }
  }

  // Get MAC.
  uint8_t mac[6] = { 0 };
  char macAddress[macStringSize] = { '\0' };
  {
    wifi_get_macaddr(STATION_IF, mac);
    const uint32_t macAddressSize = snprintf(macAddress, sizeof(macAddress), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const bool macValid = (macAddressSize >= 0 && macAddressSize < static_cast<int32_t>(sizeof(macAddress)));
    if(serialPort) { serialPort->printf_P(PSTR("%sMake string from MAC:%s\r\n"), INIT_PREFIX, macValid ? OK_STATE : ERR_STATE); }
    if(!macValid) { return false; }
  }

  // Start interface.
  WdtHandler.resetHwWdtIfPossible();
  if(interface == Interface::ETHERNET) {
    WiFi.mode(WIFI_OFF);
    ethInt.setDefault();         // default route set through this interface
    const bool ethInit = ethInt.begin(mac);
    if(serialPort) { serialPort->printf_P(PSTR("%sInitialising ethernet modul:%s\r\n"), ETH_PREFIX, ethInit ? OK_STATE : ERR_STATE); }
    if(!ethInit) { return false; }
    if(serialPort) { serialPort->printf_P(PSTR("%sConnecting to router"), ETH_PREFIX); }
    while(!ethInt.connected()) {
      yield();
      if(serialPort) { serialPort->print(loadingMark); }
      delay(300);
    }
    if(serialPort) { serialPort->printf_P(PSTR("%s\r\n"), ethInt.connected() ? OK_STATE : ERR_STATE); }
    if(!ethInt.connected()) { return false; }
    if(serialPort) {
      serialPort->printf_P(PSTR("  IP: %s\r\n"), ethInt.localIP().toString().c_str());
      serialPort->printf_P(PSTR("  GW: %s\r\n"), ethInt.gatewayIP().toString().c_str());
      serialPort->printf_P(PSTR("  SNM: %s\r\n"), ethInt.subnetMask().toString().c_str());
    }
  }
  else if(interface == Interface::WIFI) {
    const bool wifiInit = WiFi.mode(WIFI_STA);
    if(serialPort) { serialPort->printf_P(PSTR("%sInitialising wifi:%s\r\n"), WIFI_PREFIX, wifiInit ? OK_STATE : ERR_STATE); }
    if(!wifiInit) { return false; }
    WiFi.setAutoReconnect(true);
    if(!startWifi()) { return false; }
    if(serialPort) { serialPort->printf_P(PSTR("%sConnecting to router"), WIFI_PREFIX); }
    while(WiFi.status() != WL_CONNECTED) {
      yield();
      if(serialPort) { serialPort->print(loadingMark); }
      delay(300);
    }
    if(serialPort) { serialPort->printf_P(PSTR("%s\r\n"), (WiFi.status() == WL_CONNECTED) ? OK_STATE : ERR_STATE); }
    if(WiFi.status() != WL_CONNECTED) { return false; }
    if(serialPort) {
      serialPort->printf_P(PSTR("  IP: %s\r\n"), WiFi.localIP().toString().c_str());
      serialPort->printf_P(PSTR("  GW: %s\r\n"), WiFi.gatewayIP().toString().c_str());
      serialPort->printf_P(PSTR("  SNM: %s\r\n"), WiFi.subnetMask().toString().c_str());
    }
  }
  else {
    return false;
  }
  if(serialPort) { serialPort->printf_P(PSTR("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); }
  usedInterface = interface;

  // Set time via NTP, as required for x.509 validation.
  yield();
  WdtHandler.resetHwWdtIfPossible();
  {
    if(serialPort) { serialPort->printf_P(PSTR("%sWaiting for NTP time sync"), NTP_PREFIX); }
    configTime(0, 0, "0.hu.pool.ntp.org", "1.hu.pool.ntp.org", "2.hu.pool.ntp.org");
    time_t nowSecs = time(nullptr);
    while(nowSecs < 8 * 3600 * 2) {
      delay(500);
      if(serialPort) { serialPort->print(loadingMark); }
      nowSecs = time(nullptr);
    }
    tm timeinfo;
    gmtime_r(&nowSecs, &timeinfo);
    if(serialPort) {
      serialPort->printf_P(PSTR("\r\n%sCurrent UTC time: %s"), NTP_PREFIX, asctime(&timeinfo));
      serialPort->printf_P(PSTR("%sUTC ISO format: %s\r\n"), NTP_PREFIX, getISODateTime());
    }
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
    if(serialPort) {
      serialPort->printf_P(PSTR("%sClient name:%s\r\n"), MQTT_PREFIX, clientNameValid ? OK_STATE : ERR_STATE);
      serialPort->printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.clientName, clientNameSize);
      serialPort->printf_P(PSTR("%sSender topic:%s\r\n"), MQTT_PREFIX, senderTopicValid ? OK_STATE : ERR_STATE);
      serialPort->printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.senderTopic, senderTopicSize);
      serialPort->printf_P(PSTR("%sReceiver topic:%s\r\n"), MQTT_PREFIX, receiverTopicValid ? OK_STATE : ERR_STATE);
      serialPort->printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.receiverTopic, receiverTopicSize);
      serialPort->flush();
    }
    if(!clientNameValid) { return false; }
    if(!senderTopicValid) { return false; }
    if(!receiverTopicValid) { return false; }
  }

  if(!connect()) { return false; }
  mqttClient.setCallback([this](const char* topic, uint8_t* payload, uint32_t length) { receiveMqttMessage(topic, payload, length); });
  Connectivity::MqttComBase::setMqttSender([this](const char* subTopic, const char* payload) { sendMqttMessage(subTopic, payload); });

  {
    char versionString[64];
    const int32_t versionStringSize = snprintf_P(versionString, sizeof(versionString), PSTR("{""\"CPP\":%u,\"FW\":%hu,\"GH\":\"%x\",\"VCC\":%hu""}"), cppVersion, fwVersion, gitHash, ESP.getVcc());
    const bool versionStringValid = (versionStringSize >= 0 && versionStringSize < static_cast<int32_t>(sizeof(versionString)));
    if(!versionStringValid) { return false; }
    common.messageSend(versionString);
  }

  WdtHandler.resetHwWdtIfPossible();
  if(serialPort) { serialPort->printf_P(PSTR("%sInit registered objects:\r\n"), INIT_PREFIX); }
  for(uint8_t i = 0; messageMap[i] != nullptr; ++i) {
    Connectivity::MqttComBase* currentObject = messageMap[i];
    const bool beginResult = currentObject->begin();
    if(serialPort) { serialPort->printf_P(PSTR("  %hu. %s ->%s\r\n"), i, currentObject->getClassId(), beginResult ? OK_STATE : ERR_STATE); }
  }

  debugLed.stopTicker();
  WdtHandler.resetHwWdtIfPossible();
  return true;
}

bool Connectivity::startWifi() {
  const bool wifiFileExists = LittleFS.exists(FPSTR(wifiFileLocation));
  if(serialPort) {
    serialPort->printf_P(PSTR("%sCheck wifi config:\r\n"), FS_PREFIX);
    serialPort->printf_P(PSTR("  %s ->%s\r\n"), wifiFileLocation, wifiFileExists ? OK_STATE : ERR_STATE);
  }
  if(!wifiFileExists) { return false; }

  File wifiFile = LittleFS.open(FPSTR(wifiFileLocation), "r");
  if(serialPort) { serialPort->printf_P(PSTR("%sOpening: %s%s\r\n"), FS_PREFIX, wifiFile.fullName(), wifiFile ? OK_STATE : ERR_STATE); }
  if(!wifiFile) { wifiFile.close(); return false; }

  StaticJsonDocument<256> wifiJson;
  DeserializationError deserializationError = deserializeJson(wifiJson, wifiFile);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(deSerResult) {
    const char* ssid = wifiJson[F("ssid")].as<const char*>();
    const char* pass = wifiJson[F("password")].as<const char*>();
    WiFi.begin(ssid, pass);
  }
  if(serialPort) { serialPort->printf_P(PSTR("%sSerialize file:%s\r\n"), JSON_PREFIX, deSerResult ? OK_STATE : ERR_STATE); }
  wifiFile.close();
  return deSerResult;
}

bool Connectivity::connect() {
  // Open cert.
  X509List cert(mqttSettings::caCert);
  tcpClient.setTrustAnchors(&cert);
  tcpClient.setTimeout(5000);

  // TCP connection.
  //tcpClient.getLastSSLError() == BR_ERR_OK ?
  yield();
  const bool tcpStopResult = tcpClient.stop(2000);
  if(serialPort) { serialPort->printf_P(PSTR("%sReset connection for fresh start%s\r\n"), TCP_PREFIX, tcpStopResult ? OK_STATE : ERR_STATE); }
  if(!tcpStopResult) { return false; }
  const bool tcpConResult = tcpClient.connect(mqttCredentials.serverName, mqttCredentials.serverPort);
  if(serialPort) { serialPort->printf_P(PSTR("%sConnecting to: %s:%hu%s\r\n"), TCP_PREFIX, mqttCredentials.serverName, mqttCredentials.serverPort, tcpConResult ? OK_STATE : ERR_STATE); }
  if(!tcpConResult) { return false; }

  // MQTT connection.
  mqttClient.setServer(mqttCredentials.serverName, mqttCredentials.serverPort);
  const bool mqttConResult = mqttClient.connect(mqttCredentials.clientName, mqttCredentials.userName, mqttCredentials.password);
  if(serialPort) { serialPort->printf_P(PSTR("%sConnecting to MQTT broker:%s\r\n  State: %s\r\n"), MQTT_PREFIX, mqttConResult ? OK_STATE : ERR_STATE, getMqttStatusStr(mqttClient.state())); }
  if(!mqttConResult) { return false; }
  const bool subResult = mqttClient.subscribe(mqttCredentials.receiverTopic, 1);
  if(serialPort) { serialPort->printf_P(PSTR("%sSubscription:%s\r\n"), MQTT_PREFIX, subResult ? OK_STATE : ERR_STATE); }
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
      debugLed.startTicker(250);
      timeTracker.startTime();
    }
    if(serialPort) { serialPort->printf_P(PSTR("%sDevice is: %s\r\n"), RUN_PREFIX, isDeviceOnline ? F("ONLINE") : F("OFFLINE")); }
  }
  if(timeTracker.isGoalReached()) {
    if(serialPort) { serialPort->printf_P(PSTR("%sDevice is offline since: %ums\r\n"), RUN_PREFIX, timeTracker.getElapsedTime()); }
    common.restartESP();
  }
  if(loopTimeTracker.isGoalReached()) {
    const uint32_t loopTime = loopTimeTracker.stopTime();
    if(serialPort) { serialPort->printf_P(PSTR("%sMax loop time is: %ums\r\n"), RUN_PREFIX, loopTime); }
    loopTimeTracker.setGoal(loopTime + 1);
  }
}

bool Connectivity::loopSimple() {
  yield();
  WdtHandler.resetHwWdt();

  static wl_status_t actualInterfaceStatus = WL_DISCONNECTED;
  const char* intPrefix;
  if(usedInterface == Interface::ETHERNET) { actualInterfaceStatus = ethInt.status(); intPrefix = ETH_PREFIX; }
  else if(usedInterface == Interface::WIFI) { actualInterfaceStatus = WiFi.status();  intPrefix = WIFI_PREFIX; }
  else { return false; }
  if(interfaceStatus != actualInterfaceStatus) {
    if(serialPort) { serialPort->printf_P(PSTR("%sStatus changed: %s -> %s\r\n"), intPrefix, getIntStatusStr(interfaceStatus), getIntStatusStr(actualInterfaceStatus)); }
    interfaceStatus = actualInterfaceStatus;
    if(interfaceStatus == WL_CONNECTED) { connect(); }
    else { mqttClient.disconnect(); }
  }

  const int8_t actualMqttState = mqttClient.state();
  if(mqttState != actualMqttState) {
    if(serialPort) { serialPort->printf_P(PSTR("%sStatus changed: %s -> %s\r\n"), MQTT_PREFIX, getMqttStatusStr(mqttState), getMqttStatusStr(actualMqttState)); }
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

  for(uint8_t i = 0; messageMap[i] != nullptr; ++i) {
    Connectivity::MqttComBase* currentObject = messageMap[i];
    currentObject->loop();
  }

  return ((interfaceStatus == WL_CONNECTED) && (mqttState == MQTT_CONNECTED));
}

bool Connectivity::getConnectionState() { return isDeviceOnline; }

void Connectivity::receiveMqttMessage(const char* topic, uint8_t* payload, uint32_t length) {
  const char* classID = strrchr(topic, '/') + 1;
  if(!classID) { return; }
  for(uint8_t i = 0; messageMap[i] != nullptr; ++i) {
    Connectivity::MqttComBase* currentObject = messageMap[i];
    if (currentObject != nullptr && strcmp(currentObject->getClassId(), classID) == 0) {
      currentObject->messageReceived(payload, length);
      return;
    }
  }
  if(serialPort) { serialPort->printf_P(PSTR("%sNo handler -> %s\r\n"), MQTT_PREFIX, classID); }
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
  return buffer;
}

bool Connectivity::registerCallback(Connectivity::MqttComBase* obj) {
  if(!obj) { return false; }
  if(messageMapPointer >= messageMapSize) { return false; }
  messageMap[messageMapPointer] = obj;
  messageMapPointer++;
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
    case WL_WRONG_PASSWORD: { return WL_WRONG_PASSWORD_STR; } break;
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

//////////////////// -- WDT class-- ////////////////////

void Connectivity::WdtWrapper::enableHwWdt() {
  wdt_disable();                                          // Disables the SW watchdog and enables the HW watchdog -> ~8400ms
}

void Connectivity::WdtWrapper::resetHwWdt() {
  this->enabledResetNumber = 0;
  wdt_reset();
}

void Connectivity::WdtWrapper::resetHwWdtIfPossible() {
  if(this->enabledResetNumber > 0) {
    this->enabledResetNumber--;
    wdt_reset();
  }
}
void Connectivity::WdtWrapper::setEnabledResetNumber(uint8_t enabledResetNumber) {
  this->enabledResetNumber = enabledResetNumber;
}

//////////////////// -- Debug LED class-- ////////////////////

Connectivity::DebugLED::DebugLED(uint8_t ledPin, bool ledOnState) : ledPin_(ledPin), ledOnState_(ledOnState) {
  pinMode(this->ledPin_, OUTPUT);
}

void Connectivity::DebugLED::ledOn() { this->ledOnState_ ? ledLow() : ledHigh(); }

void Connectivity::DebugLED::ledOff() { this->ledOnState_ ? ledHigh() : ledLow(); }

void Connectivity::DebugLED::startTicker(uint32_t tickInterval_ms) {
  ledOff();
  this->ledTicker.attach_ms(tickInterval_ms, [this](){ ledToggle(); });
}

void Connectivity::DebugLED::stopTicker() {
  this->ledTicker.detach();
  ledOff();
}

void Connectivity::DebugLED::ledToggle() {
  digitalWrite(this->ledPin_, !digitalRead(this->ledPin_));   // LED pin toggle.
}

void Connectivity::DebugLED::ledHigh() {
  digitalWrite(this->ledPin_, HIGH);                          // LED pin high.
}
void Connectivity::DebugLED::ledLow() {
  digitalWrite(this->ledPin_, LOW);                           // LED pin low.
}

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

//////////////////// -- CRC32 class-- ////////////////////

Connectivity::Crc32::Crc32(uint32_t initValue, uint32_t polynomial) :
  crc_(initValue),
  polynomial_(polynomial) {}

void Connectivity::Crc32::next(uint8_t value) {
    crc_ ^= (uint32_t)value;
    for(uint8_t i = 0; i < 8; i++) {
      if(crc_ & 1) {
        crc_ = (crc_ >> 1) ^ polynomial_;
      }
      else {
        crc_ >>= 1;
      }
    }
  }

  void Connectivity::Crc32::next(const uint8_t* values, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
      next(values[i]);
    }
  }

  uint32_t Connectivity::Crc32::get() const { return ~crc_; } // Final CRC32 value is complemented

  uint32_t Connectivity::Crc32::calculate(const uint8_t *data, uint16_t length) {
    Connectivity::Crc32 crc;
    crc.next(data, length);
    return crc.get();
  }

//////////////////// -- Base64 class-- ////////////////////

uint32_t Connectivity::Base64::encodedLength(uint32_t plainLength) {
  int32_t n = plainLength;
  return (n + 2 - ((n + 2) % 3)) / 3 * 4;
}

uint32_t Connectivity::Base64::decodedLength(const uint8_t input[], uint32_t inputLength) {
  int32_t i = 0;
  int32_t numEq = 0;
  for(i = inputLength - 1; input[i] == '='; i--) { numEq++; }
  return ((6 * inputLength) / 8) - numEq;
}

uint32_t Connectivity::Base64::encodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength) {
  int32_t i = 0, j = 0;
  int32_t encodedLength = 0;
  uint8_t A3[3];
  uint8_t A4[4];

  while(inputLength--) {
    A3[i++] = *(input++);
    if(i == 3) {
      fromA3ToA4(A4, A3);
      for(i = 0; i < 4; i++) {
        output[encodedLength++] = pgm_read_byte(&_Base64AlphabetTable[A4[i]]);
      }
      i = 0;
    }
  }
  if(i) {
    for(j = i; j < 3; j++) {
      A3[j] = '\0';
    }
    fromA3ToA4(A4, A3);
    for(j = 0; j < i + 1; j++) {
      output[encodedLength++] = pgm_read_byte(&_Base64AlphabetTable[A4[j]]);
    }
    while((i++ < 3)) {
      output[encodedLength++] = '=';
    }
  }
  output[encodedLength] = '\0';
  return encodedLength;
}

uint32_t Connectivity::Base64::decodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength) {
  int32_t i = 0, j = 0;
  uint32_t decodedLength = 0;
  uint8_t A3[3];
  uint8_t A4[4];

  while(inputLength--) {
    if(*input == '=') { break; }
    A4[i++] = *(input++);
    if(i == 4) {
      for(i = 0; i < 4; i++) {
        A4[i] = lookupTable(A4[i]);
      }
      fromA4ToA3(A3, A4);
      for(i = 0; i < 3; i++) {
        output[decodedLength++] = A3[i];
      }
      i = 0;
    }
  }
  if(i) {
    for(j = i; j < 4; j++) {
      A4[j] = '\0';
    }
    for(j = 0; j < 4; j++) {
      A4[j] = lookupTable(A4[j]);
    }
    fromA4ToA3(A3, A4);
    for(j = 0; j < i - 1; j++) {
      output[decodedLength++] = A3[j];
    }
  }
  output[decodedLength] = '\0';
  return decodedLength;
}

void Connectivity::Base64::fromA3ToA4(uint8_t* A4, uint8_t* A3) {
  A4[0] = (A3[0] & 0xfc) >> 2;
  A4[1] = ((A3[0] & 0x03) << 4) + ((A3[1] & 0xf0) >> 4);
  A4[2] = ((A3[1] & 0x0f) << 2) + ((A3[2] & 0xc0) >> 6);
  A4[3] = (A3[2] & 0x3f);
}

void Connectivity::Base64::fromA4ToA3(uint8_t* A3, uint8_t* A4) {
  A3[0] = (A4[0] << 2) + ((A4[1] & 0x30) >> 4);
  A3[1] = ((A4[1] & 0xf) << 4) + ((A4[2] & 0x3c) >> 2);
  A3[2] = ((A4[2] & 0x3) << 6) + A4[3];
}

uint8_t Connectivity::Base64::lookupTable(char c) {
  if(c >='A' && c <='Z') return c - 'A';
  if(c >='a' && c <='z') return c - 71;
  if(c >='0' && c <='9') return c + 4;
  if(c == '+') return 62;
  if(c == '/') return 63;
  return -1;
}

const char Connectivity::Base64::_Base64AlphabetTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789+/";

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
  if(!fileName) { stop(true); return false; }
  this->fileName_ = fileName;
  {
    FSInfo fsInfo;
    LittleFS.info(fsInfo);
    const uint32 freeSpace = fsInfo.totalBytes - fsInfo.usedBytes;
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
  const uint32_t decodedPreSize = Connectivity::Base64::decodedLength(reinterpret_cast<const uint8_t*>(fileData), filePieceB64Size);
  if(decodedPreSize > sizeof(decodedData)) {
    if(this->serialPort) { this->serialPort->printf_P(PSTR("%sFile piece size error!\r\n"), FILE_TRANSFER_PREFIX); }
    return false;
  }
  const uint32_t decodedPostSize = Connectivity::Base64::decodeBase64(reinterpret_cast<const uint8_t*>(fileData), decodedData, filePieceB64Size);
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
  Connectivity::Crc32 crc32;
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

std::function<void(const char*, const char*)> Connectivity::MqttComBase::mqttSender = nullptr;

Connectivity::MqttComBase::MqttComBase(const char* classID) {
  strlcpy(this->classId, classID, sizeof(this->classId));
  Connectivity::registerCallback(this);
}

void Connectivity::MqttComBase::messageSend(const char* payload) const {
  if(mqttSender) {
    mqttSender(getClassId(), payload);
  }
}

void Connectivity::MqttComBase::setMqttSender(std::function<void(const char*, const char*)> senderFunction) {
  mqttSender = senderFunction;
}

const char* Connectivity::MqttComBase::getIsoTime() {
  return Connectivity::getISODateTime();
}

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

Connectivity::Common::Common(const char* classID, Stream* serial) :
  MqttComBase(classID),
  serialPort(serial),
  dataTransfer(serial),
  externalFileName{'\0'} {}

void Connectivity::Common::messageReceived(uint8_t* payload, uint32_t length) {
  StaticJsonDocument<512> cmdJson;
  DeserializationError deserializationError = deserializeJson(cmdJson, payload, length);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(!deSerResult) {
    if(serialPort) { serialPort->printf_P(PSTR("%sDeserialisation failed!\r\n"), COMMON_PREFIX); }
    return;
  }
  else {
    const uint8_t cmd = cmdJson[F("cmd")].as<uint8_t>();
    Command command = static_cast<Command>(cmd);
    switch(command) {
      case Command::BLANK: {} break;
      case Command::RESTART: { restartESP(); } break;
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
              if(serialPort) { serialPort->printf_P(PSTR("%sWrong FW file ID: %s\r\n"), COMMON_PREFIX, binId); }
              MqttComBase::sendResponse(MqttComBase::Response::NACK, cmd);
              return;
            }
            fileNamePtr = DataTransfer::otaFwLocation;
            } break;
          case Command::WIFICFG_DT_START: { fileNamePtr = DataTransfer::wifiTempFileLocation; } break;
          case Command::EXT_FILE_DT_START: {
            const char* fileName = cmdJson[F("name")].as<const char*>();
            memset(externalFileName, '\0', sizeof(externalFileName));
            if(fileName) { memccpy(externalFileName, fileName, '\0', sizeof(externalFileName)); }
            uint32_t externalFileNameSize =  strnlen(externalFileName, sizeof(externalFileName));
            if(externalFileNameSize == 0 || externalFileNameSize >= sizeof(externalFileName)) {
              if(serialPort) { serialPort->printf_P(PSTR("%sWrong file name: missing / too long!\r\n"), COMMON_PREFIX); }
              MqttComBase::sendResponse(MqttComBase::Response::NACK, cmd);
              return;
            }
            fileNamePtr = externalFileName;
          } break;
          default: {} break;
        }
        const bool transferBeginResult = dataTransfer.begin(fileSize, fileCrc, fileNamePtr);
        MqttComBase::sendResponse((transferBeginResult ? MqttComBase::Response::ACK : MqttComBase::Response::NACK), cmd);
        if(!transferBeginResult) {
          if(serialPort) { serialPort->printf_P(PSTR("%sCan't begin file transfer:\r\n  Name: %s\r\n"), COMMON_PREFIX, fileNamePtr); }
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
          if(serialPort) { serialPort->printf_P(PSTR("%sFile storing failed!\r\n"), COMMON_PREFIX); }
        }
      } break;
      case Command::FW_DT_END:
      case Command::WIFICFG_DT_END:
      case Command::EXT_FILE_DT_END: {
        const bool validityCheckResult = dataTransfer.checkValidity();
        MqttComBase::sendResponse((validityCheckResult ? MqttComBase::Response::ACK : MqttComBase::Response::NACK), cmd);
        if(!validityCheckResult) {
          if(serialPort) { serialPort->printf_P(PSTR("%sStored file is not valid!\r\n"), COMMON_PREFIX); }
        }
        if(validityCheckResult && (command == Command::FW_DT_END)) { restartESP(); }
      } break;
    };
  }
}

bool Connectivity::Common::begin() { return true; }

bool Connectivity::Common::loop() { return true; }

void Connectivity::Common::messageSend(const char* payload) const {
  MqttComBase::messageSend(payload);
}

void Connectivity::Common::restartESP() {
  if(serialPort) {
    serialPort->printf_P(PSTR("%sRestarting...\r\n"), COMMON_PREFIX);
    serialPort->flush();                              // Sends out data from serial buffer, before reset.
  }
  ESP.restart();
  while(true) {};                                     // Prevent doing anything before restart.
}
