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

Connectivity::MqttComBase* Connectivity::messageMap[10] = { nullptr };
uint8_t Connectivity::messageMapPointer = 0;

const char Connectivity::wifiFileLocation[] PROGMEM         = "/config/wifi.json";
const char Connectivity::configFileLocation[] PROGMEM       = "/config/server.json";      // Config file location on FS.
const char Connectivity::configBackupFileLocation[] PROGMEM = "/config/server.json.bkp";  // Config file backup location on FS.
const char Connectivity::certFileLocation[] PROGMEM         = "/config/mosq-ca.crt";      // Used cert location on FS.
const char Connectivity::certBackupFileLocation[] PROGMEM   = "/config/mosq-ca.crt.bkp";  // Cert backup location on FS.

const char Connectivity::BASE_TOPIC[] PROGMEM               = "iot";
const char Connectivity::SENDER_TOPIC[] PROGMEM             = "dtos";
const char Connectivity::RECEIVER_TOPIC[] PROGMEM           = "stod";
const char Connectivity::LOG_TOPIC[] PROGMEM                = "log";
const char Connectivity::LOG_MSG[] PROGMEM                  = "{""\"online\":%s""}";

const char Connectivity::OK_STATE[] PROGMEM                 = " [OK]";                    // OK status.
const char Connectivity::ERR_STATE[] PROGMEM                = " [ERR]";                   // Error status.

const char Connectivity::INIT_PREFIX[] PROGMEM              = "[INIT] ";
const char Connectivity::FS_PREFIX[] PROGMEM                = "[FS] ";
const char Connectivity::ETH_PREFIX[] PROGMEM               = "[ETH] ";
const char Connectivity::WIFI_PREFIX[] PROGMEM              = "[WIFI] ";
const char Connectivity::NTP_PREFIX[] PROGMEM               = "[NTP] ";
const char Connectivity::JSON_PREFIX[] PROGMEM              = "[JSON] ";
const char Connectivity::TCP_PREFIX[] PROGMEM               = "[TCP] ";
const char Connectivity::MQTT_PREFIX[] PROGMEM              = "[MQTT] ";

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
  common("common", serial)
{
  WdtHandler.enableHwWdt();
}

bool Connectivity::begin(Interface interface) {
  WdtHandler.setEnabledResetNumber(3);
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
  }

  // Get MAC.
  uint8_t mac[6] = { 0 };
  char macAddress[13] = { '\0' };
  {
    wifi_get_macaddr(STATION_IF, mac);
    const uint32_t macAddressSize = snprintf(macAddress, sizeof(macAddress), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const bool macValid = (macAddressSize >= 0 && macAddressSize < static_cast<int32_t>(sizeof(macAddress)));
    if(serialPort) { serialPort->printf_P(PSTR("%sMake string from MAC:%s\r\n"), INIT_PREFIX, macValid ? OK_STATE : ERR_STATE); }
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
      if(serialPort) { serialPort->print(F(".")); }
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
      if(serialPort) { serialPort->print(F(".")); }
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
      if(serialPort) { serialPort->print(F(".")); }
      nowSecs = time(nullptr);
    }
    tm timeinfo;
    gmtime_r(&nowSecs, &timeinfo);
    if(serialPort) {
      serialPort->printf_P(PSTR("\r\n%sCurrent UTC time: %s"), NTP_PREFIX, asctime(&timeinfo));
      serialPort->printf_P(PSTR("%sUTC ISO format: %s\r\n"), NTP_PREFIX, getISODateTime().c_str());
    }
  }

  // Setup MQTT topics.
  {
    const int32_t clientNameSize = snprintf_P(mqttCredentials.clientName, sizeof(mqttCredentials.clientName), "%s_%s", DEVICE_TYPE, macAddress);
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

  if(!checkFiles()) { return false; }
  if(!loadConfig(ConfigFile::NORMAL)) { return false; }
  if(!connect(CertFile::NORMAL)) { return false; }

  mqttClient.setCallback([this](const char* topic, uint8_t* payload, uint32_t length) { receiveMqttMessage(topic, payload, length); });
  Connectivity::MqttComBase::setMqttSender([this](const char* subTopic, const char* payload) { sendMqttMessage(subTopic, payload); });

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
    const uint8_t channel = wifiJson[F("channel")].as<const uint8_t>();
    JsonArray bssidArray = wifiJson[F("bssid")];
    uint8_t bssid[6];
    for(uint8_t i = 0; i < bssidArray.size(); i++) {
      bssid[i] = bssidArray[i];
    }
    WiFi.begin(ssid, pass, channel, bssid);
  }
  if(serialPort) { serialPort->printf_P(PSTR("%sSerialize file:%s\r\n"), JSON_PREFIX, deSerResult ? OK_STATE : ERR_STATE); }
  wifiFile.close();
  return deSerResult;
}

bool Connectivity::checkFiles() {
  // Check for config.
  const bool configFileExists = LittleFS.exists(FPSTR(configFileLocation));
  const bool configBackupFileExists = LittleFS.exists(FPSTR(configBackupFileLocation));
  if(serialPort) {
    serialPort->printf_P(PSTR("%sCheck config files:\r\n"), FS_PREFIX);
    serialPort->printf_P(PSTR("  %s ->%s\r\n"), configFileLocation, configFileExists ? OK_STATE : ERR_STATE);
    serialPort->printf_P(PSTR("  %s ->%s\r\n"), configBackupFileLocation, configBackupFileExists ? OK_STATE : ERR_STATE);
  }

  // Check for cert.
  const bool certFileExists = LittleFS.exists(FPSTR(certFileLocation));
  const bool certBackupFileExists = LittleFS.exists(FPSTR(certBackupFileLocation));
  if(serialPort) {
    serialPort->printf_P(PSTR("%sCheck certification files:\r\n"), FS_PREFIX);
    serialPort->printf_P(PSTR("  %s ->%s\r\n"), certFileLocation, certFileExists ? OK_STATE : ERR_STATE);
    serialPort->printf_P(PSTR("  %s ->%s\r\n"), certBackupFileLocation, certBackupFileExists ? OK_STATE : ERR_STATE);
  }

  return ((configFileExists || configBackupFileExists) && (certFileExists || certBackupFileExists));
}

bool Connectivity::loadConfig(ConfigFile actualConfig) {
  File configFile;
  if(actualConfig == ConfigFile::NORMAL) {
    configFile = LittleFS.open(FPSTR(configFileLocation), "r");
  }
  else if(actualConfig == ConfigFile::BACKUP) {
    configFile = LittleFS.open(FPSTR(configBackupFileLocation), "r");
  }
  else {
    return false;
  }
  if(serialPort) { serialPort->printf_P(PSTR("%sOpening: %s%s\r\n"), FS_PREFIX, configFile.fullName(), configFile ? OK_STATE : ERR_STATE); }
  if(!configFile) { configFile.close(); return false; }

  StaticJsonDocument<256> configJson;
  DeserializationError deserializationError = deserializeJson(configJson, configFile);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(deSerResult) {
    strlcpy(mqttCredentials.userName, configJson[F("mqttUserName")].as<const char*>(), sizeof(mqttCredentials.userName));
    strlcpy(mqttCredentials.password, configJson[F("mqttPassword")].as<const char*>(), sizeof(mqttCredentials.password));
    strlcpy(mqttCredentials.serverName, configJson[F("mqttServerName")].as<const char*>(), sizeof(mqttCredentials.serverName));
    mqttCredentials.serverPort = configJson[F("mqttServerPort")].as<uint16_t>();
  }
  if(serialPort) { serialPort->printf_P(PSTR("%sSerialize file:%s\r\n"), JSON_PREFIX, deSerResult ? OK_STATE : ERR_STATE); }
  configFile.close();
  return deSerResult;
}

bool Connectivity::connect(CertFile actualCert) {
  // Open cert.
  File certFile;
  if(actualCert == CertFile::NORMAL) {
    certFile = LittleFS.open(FPSTR(certFileLocation), "r");
  }
  else if(actualCert == CertFile::BACKUP) {
    certFile = LittleFS.open(FPSTR(certBackupFileLocation), "r");
  }
  else {
    return false;
  }
  if(serialPort) { serialPort->printf_P(PSTR("%sOpening: %s%s\r\n"), FS_PREFIX, certFile.fullName(), certFile ? OK_STATE : ERR_STATE); }
  if(!certFile) { certFile.close(); return false; }

  X509List cert(certFile);
  tcpClient.setTrustAnchors(&cert);
  tcpClient.setTimeout(5000);
  certFile.close();

  // TCP connection.
  //tcpClient.getLastSSLError() == BR_ERR_OK ?
  yield();
  const bool tcpStopResult = tcpClient.stop(2000);
  if(serialPort) { serialPort->printf_P(PSTR("%sReset connection for fresh start%s\r\n"), TCP_PREFIX, tcpStopResult ? OK_STATE : ERR_STATE); }
  const bool tcpConResult = tcpClient.connect(mqttCredentials.serverName, mqttCredentials.serverPort);
  if(serialPort) { serialPort->printf_P(PSTR("%sConnecting to: %s:%hu%s\r\n"), TCP_PREFIX, mqttCredentials.serverName, mqttCredentials.serverPort, tcpConResult ? OK_STATE : ERR_STATE); }
  if(!tcpConResult) { return false; }

  // MQTT connection.
  char logTopic[sizeof(mqttCredentials.senderTopic) + sizeof(LOG_TOPIC)] = { '\0' };
  const int32_t logTopicSize = snprintf_P(logTopic, sizeof(logTopic), "%s/%s", mqttCredentials.senderTopic, LOG_TOPIC);
  const bool logTopicValid = (logTopicSize >= 0 && logTopicSize < static_cast<int32_t>(sizeof(logTopic)));
  if(!logTopicValid) { return false; }

  char logMsg[sizeof(LOG_MSG) + 8] = { '\0' };
  int32_t logMsgSize = snprintf_P(logMsg, sizeof(logMsg), LOG_MSG, "false");
  bool logMsgValid = (logMsgSize >= 0 && logMsgSize < static_cast<int32_t>(sizeof(logMsg)));
  if(!logMsgValid) { return false; }

  mqttClient.setServer(mqttCredentials.serverName, mqttCredentials.serverPort);
  const bool mqttConResult = mqttClient.connect(mqttCredentials.clientName, mqttCredentials.userName, mqttCredentials.password, logTopic, 1, false, logMsg, true);
  if(serialPort) { serialPort->printf_P(PSTR("%sConnecting to MQTT broker:%s State: %d\r\n"), MQTT_PREFIX, mqttConResult ? OK_STATE : ERR_STATE, mqttClient.state()); }
  if(!mqttConResult) { return false; }
  const bool subResult = mqttClient.subscribe(mqttCredentials.receiverTopic, 1);
  if(serialPort) { serialPort->printf_P(PSTR("%sSubscription:%s\r\n"), MQTT_PREFIX, subResult ? OK_STATE : ERR_STATE); }
  if(!subResult) { return false; }

  logMsgSize = snprintf_P(logMsg, sizeof(logMsg), LOG_MSG, "true");
  logMsgValid = (logMsgSize >= 0 && logMsgSize < static_cast<int32_t>(sizeof(logMsg)));
  if(!logMsgValid) { return false; }
  return mqttClient.publish(logTopic, logMsg);
}

bool Connectivity::loop() {
  for(uint8_t i = 0; messageMap[i] != nullptr; ++i) {
    Connectivity::MqttComBase* currentObject = messageMap[i];
    currentObject->loop();
  }

  yield();
  WdtHandler.resetHwWdt();

  static wl_status_t actualInterfaceStatus = WL_DISCONNECTED;
  const char* intPrefix;
  if(usedInterface == Interface::ETHERNET) { actualInterfaceStatus = ethInt.status(); intPrefix = ETH_PREFIX; }
  else if(usedInterface == Interface::WIFI) { actualInterfaceStatus = WiFi.status();  intPrefix = WIFI_PREFIX; }
  else { return false; }
  if(interfaceStatus != actualInterfaceStatus) {
    if(serialPort) { serialPort->printf_P(PSTR("%sStatus changed: %hd -> %hd\r\n"), intPrefix, interfaceStatus, actualInterfaceStatus); }
    interfaceStatus = actualInterfaceStatus;
    if(interfaceStatus != WL_CONNECTED || mqttState != MQTT_CONNECTED) {
      Connectivity::MqttComBase::setConState(false);
      debugLed.startTicker(250);
    }
  }
  if(interfaceStatus != WL_CONNECTED) { return false; }

  const int8_t actualMqttState = mqttClient.state();
  if(mqttState != actualMqttState) {
    if(serialPort) { serialPort->printf_P(PSTR("%sStatus changed: %hd -> %hd\r\n"), MQTT_PREFIX, mqttState, actualMqttState); }
    mqttState = actualMqttState;
    if(mqttState == MQTT_CONNECTED) {
      Connectivity::MqttComBase::setConState(true);
      debugLed.stopTicker();
    }
  }

  if(!mqttClient.loop()) {
    if(mqttState < MQTT_CONNECTED) {
      static uint32_t reconnectTimer = millis();
      if(millis() - reconnectTimer >= 10000) {
        reconnectTimer = millis();
        connect(CertFile::NORMAL);
      }
    }
  }

  return ((interfaceStatus == WL_CONNECTED) && (mqttState == MQTT_CONNECTED));
}

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

String Connectivity::getISODateTime() {
  const time_t time_ = time(nullptr);
  char buffer[30];
  struct tm * timeinfo;
  timeinfo = gmtime(&time_); // Convert time to UTC time structure
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", timeinfo); // Format as ISO UTC string
  return String(buffer);
}

bool Connectivity::registerCallback(Connectivity::MqttComBase* obj) {
  if(!obj) { return false; }
  if(messageMapPointer >= sizeof(messageMap)) { return false; }
  messageMap[messageMapPointer] = obj;
  messageMapPointer++;
  return true;
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
  if(ledPin != 255) { pinMode(ledPin, OUTPUT); }
}

void Connectivity::DebugLED::ledOn() { ledOnState_ ? ledLow() : ledHigh(); }

void Connectivity::DebugLED::ledOff() { ledOnState_ ? ledHigh() : ledLow(); }

void Connectivity::DebugLED::startTicker(uint32_t tickInterval_ms) {
  if(ledPin_ == 255) { return; }
  ledOff();
  ledTicker.attach_ms(tickInterval_ms, [this](){ ledToggle(); });
}

void Connectivity::DebugLED::stopTicker() {
  if(ledPin_ == 255) { return; }
  ledTicker.detach();
  ledOff();
}

void Connectivity::DebugLED::ledToggle() {
  if(ledPin_ != 255) { (GPO  ^=  (1 << ledPin_)); }     // LED pin toggle.
}

void Connectivity::DebugLED::ledHigh() {
  if(ledPin_ != 255) { (GPOS |=  (1 << ledPin_)); }     // LED pin high.
}
void Connectivity::DebugLED::ledLow() {
  if(ledPin_ != 255) { (GPOC |=  (1 << ledPin_)); }     // LED pin low.
}

//////////////////// -- CRC32 class-- ////////////////////

Connectivity::Crc32::Crc32() : crc(0xFFFFFFFF) {} // Initialize CRC32 value

void Connectivity::Crc32::next(uint8_t value) {
    crc ^= (uint32_t)value;
    for(uint8_t i = 0; i < 8; i++) {
      if(crc & 1) {
        crc = (crc >> 1) ^ polynomial;
      }
      else {
        crc >>= 1;
      }
    }
  }

  void Connectivity::Crc32::next(const uint8_t* values, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
      next(values[i]);
    }
  }

  uint32_t Connectivity::Crc32::get() const { return ~crc; } // Final CRC32 value is complemented

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

const char Connectivity::DataTransfer::FILE_TRANSFER_PREFIX[] PROGMEM              = "[FT] ";
const char Connectivity::DataTransfer::OTA_FW_LOCATION[] PROGMEM         = "/config/espFirmware.bin";

Connectivity::DataTransfer::DataTransfer(Stream* serial) :
  serialPort(serial),
  fileSize_(0),
  fileCrc_(0),
  nextFilePieceNumber_(-1),
  remainingFileSize_(0),
  fileName_(nullptr),
  fileTransferStarted_(false) {}

bool Connectivity::DataTransfer::begin(uint32_t fileSize, uint32_t fileCrc, const char* fileName, bool deleteExistingFile) {
  if(this->fileTransferStarted_) { return false; }
  this->fileTransferStarted_ = true;
  this->fileSize_ = fileSize;
  this->fileCrc_ = fileCrc;
  this->nextFilePieceNumber_ = 0;
  this->remainingFileSize_ = fileSize;
  if(!fileName) { stop(true); return false; }
  this->fileName_ = fileName;

  const bool fileExists = LittleFS.exists(FPSTR(this->fileName_));
  if(fileExists && deleteExistingFile) {
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
  if(this->serialPort) { this->serialPort->printf_P(PSTR("%sFile transfer stopped!\r\n"), FILE_TRANSFER_PREFIX); }

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
  if(this->fileName_ != OTA_FW_LOCATION) {
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

bool Connectivity::MqttComBase::isOnline = false;
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

void Connectivity::MqttComBase::setConState(bool state) { isOnline = state; }

bool Connectivity::MqttComBase::getConState() { return isOnline; }

//////////////////// -- Common class-- ////////////////////

const char Connectivity::Common::COMMON_PREFIX[] PROGMEM              = "[COMMON] ";

Connectivity::Common::Common(const char* classID, Stream* serial) : MqttComBase(classID), serialPort(serial), ota(serial) {}

void Connectivity::Common::messageReceived(uint8_t* payload, uint32_t length) {
  StaticJsonDocument<512> cmdJson;
  DeserializationError deserializationError = deserializeJson(cmdJson, payload, length);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(!deSerResult){
    if(serialPort) { serialPort->printf_P(PSTR("%sDeserialisation failed!\r\n"), COMMON_PREFIX); }
  }
  if(deSerResult) {
    const uint8_t cmd = cmdJson["cmd"].as<uint8_t>();
    Command command = static_cast<Command>(cmd);
    switch(command) {
      case Command::BLANK: {} break;
      case Command::RESTART: { restartESP(); } break;
      case Command::OTA_START: {
        const uint32_t fwSize = cmdJson[F("fwSize")].as<uint32_t>();
        const uint32_t fwCrc = cmdJson[F("crc32")].as<uint32_t>();
        const bool otaBeginResult = ota.begin(fwSize, fwCrc, DataTransfer::OTA_FW_LOCATION, true);
        MqttComBase::sendResponse((otaBeginResult ? MqttComBase::Response::ACK : MqttComBase::Response::NACK), cmd);
        if(!otaBeginResult) {
          if(serialPort) { serialPort->printf_P(PSTR("%sCan't begin OTA!\r\n"), COMMON_PREFIX); }
          return;
        }
      } break;
      case Command::OTA_DATA: {
        uint8_t fwData[336];
        const uint32_t fwPieceNumber = cmdJson[F("piece")].as<uint32_t>();
        const char* fwDataB64 = cmdJson["data"].as<const char*>();
        const uint32_t fwDataB64Size = strlen(fwDataB64);
        const uint32_t decodedSize = Connectivity::Base64::decodedLength(reinterpret_cast<const uint8_t*>(fwDataB64), fwDataB64Size);
        if(decodedSize > sizeof(fwData)) {
          if(serialPort) { serialPort->printf_P(PSTR("%sFW piece size error!\r\n"), COMMON_PREFIX); }
          MqttComBase::sendResponse(MqttComBase::Response::NACK, cmd);
          return;
        }
        const uint32_t decodedSize2 = Connectivity::Base64::decodeBase64(reinterpret_cast<const uint8_t*>(fwDataB64), fwData, fwDataB64Size);
        if(decodedSize != decodedSize2) {
          if(serialPort) { serialPort->printf_P(PSTR("%sDecoded size check error!\r\n"), COMMON_PREFIX); }
          MqttComBase::sendResponse(MqttComBase::Response::NACK, cmd);
          return;
        }
        if(!ota.store(fwPieceNumber, fwData, decodedSize)) {
          if(serialPort) { serialPort->printf_P(PSTR("%sFW storing failed!\r\n"), COMMON_PREFIX); }
          MqttComBase::sendResponse(MqttComBase::Response::NACK, cmd);
          return;
        }
        MqttComBase::sendResponse(MqttComBase::Response::ACK, cmd);
      } break;
      case Command::OTA_END: {
        const bool validityCheckResult = ota.checkValidity();
        MqttComBase::sendResponse((validityCheckResult ? MqttComBase::Response::ACK : MqttComBase::Response::NACK), cmd);
        if(!validityCheckResult) {
          if(serialPort) { serialPort->printf_P(PSTR("%sStored FW is not valid!\r\n"), COMMON_PREFIX); }
        }
      } break;
    };
  }
}

bool Connectivity::Common::begin() { return true; }

bool Connectivity::Common::loop() { return true; }

void Connectivity::Common::restartESP() {
  if(serialPort) {
    serialPort->printf_P(PSTR("%sRestarting...\r\n"), COMMON_PREFIX);
    serialPort->flush();                              // Sends out data from serial buffer, before reset.
  }
  ESP.restart();
  delay(10000);                                       // Prevent doing anything before restart.
}
