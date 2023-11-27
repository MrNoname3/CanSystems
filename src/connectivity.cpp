#include "connectivity.hpp"
#include "MqttComBase.hpp"

MqttComBase* Connectivity::messageMap[10] = { nullptr };
uint8_t Connectivity::messageMapPointer = 0;

const char Connectivity::wifiFileLocation[] PROGMEM         = "/config/wifi.json";
const char Connectivity::configFileLocation[] PROGMEM       = "/config/config.json";      // Config file location on FS.
const char Connectivity::configBackupFileLocation[] PROGMEM = "/config/config.json.bkp";  // Config file backup location on FS.
const char Connectivity::certFileLocation[] PROGMEM         = "/cert/mosq-ca.crt";        // Used cert location on FS.
const char Connectivity::certBackupFileLocation[] PROGMEM   = "/cert/mosq-ca.crt.bkp";    // Cert backup location on FS.

const char Connectivity::BASE_TOPIC[] PROGMEM               = "iot";
const char Connectivity::SENDER_TOPIC[] PROGMEM             = "dtos";
const char Connectivity::RECEIVER_TOPIC[] PROGMEM           = "stod";

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

Connectivity::Connectivity(HardwareSerial* serial, const uint8_t ethCS) :
serialPort(serial), ethInt(ethCS), tcpClient(), mqttClient(tcpClient), usedInterface(Interface::UNKNOWN),
interfaceStatus(WL_CONNECTED), mqttState(MQTT_CONNECTED) {}

bool Connectivity::begin(Interface interface) {
  if(serialPort) { serialPort->printf_P(PSTR("%sBegin connection...\r\n"), INIT_PREFIX); }

  // Init filesystem.
  const bool initFS = LittleFS.begin();
  if(serialPort) { serialPort->printf_P(PSTR("%sInitialising filesystem:%s\r\n"), FS_PREFIX, (initFS ? OK_STATE : ERR_STATE)); }
  if(!initFS) { return false; }

  // Get MAC.
  uint8_t mac[6] = { 0 };
  char macAddress[13] = { '\0' };
  wifi_get_macaddr(STATION_IF, mac);
  const uint32_t macAddressSize = snprintf(macAddress, sizeof(macAddress), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  const bool macValid = (macAddressSize >= 0 && macAddressSize < static_cast<int32_t>(sizeof(macAddress)));
  if(serialPort) { serialPort->printf_P(PSTR("%sMake string from MAC:%s\r\n"), INIT_PREFIX, macValid ? OK_STATE : ERR_STATE); }

  // Start interface.
  if(interface == Interface::ETHERNET) {
    WiFi.mode(WIFI_OFF);
    ethInt.setDefault();         // default route set through this interface
    const bool ethInit = ethInt.begin(mac);
    if(serialPort) { serialPort->printf_P(PSTR("%sInitialising ethernet modul:%s\r\n"), ETH_PREFIX, ethInit ? OK_STATE : ERR_STATE); }
    if(!ethInit) { return false; }
    if(serialPort) { serialPort->printf_P(PSTR("%sConnecting to router"), ETH_PREFIX); }
    while(!ethInt.connected()) {
      if(serialPort) { serialPort->print(F(".")); }
      delay(300);
    }
    if(serialPort) { serialPort->printf_P(PSTR("%s\r\n"), ethInt.connected() ? OK_STATE : ERR_STATE); }
    if(!ethInt.connected()) { return false; }
    if(serialPort) { serialPort->printf_P(PSTR("  IP: %s\r\n"), ethInt.localIP().toString().c_str()); }
    if(serialPort) { serialPort->printf_P(PSTR("  GW: %s\r\n"), ethInt.gatewayIP().toString().c_str()); }
    if(serialPort) { serialPort->printf_P(PSTR("  SNM: %s\r\n"), ethInt.subnetMask().toString().c_str()); }
  }
  else if(interface == Interface::WIFI) {
    const bool wifiInit = WiFi.mode(WIFI_STA);
    if(serialPort) { serialPort->printf_P(PSTR("%sInitialising wifi:%s\r\n"), WIFI_PREFIX, wifiInit ? OK_STATE : ERR_STATE); }
    if(!wifiInit) { return false; }
    WiFi.setAutoReconnect(true);
    if(!startWifi()) { return false; }
    if(serialPort) { serialPort->printf_P(PSTR("%sConnecting to router"), WIFI_PREFIX); }
    while(WiFi.status() != WL_CONNECTED) {
      delay(300);
      Serial.print(".");
    }
    if(serialPort) { serialPort->printf_P(PSTR("%s\r\n"), (WiFi.status() == WL_CONNECTED) ? OK_STATE : ERR_STATE); }
    if(WiFi.status() != WL_CONNECTED) { return false; }
    if(serialPort) { serialPort->printf_P(PSTR("  IP: %s\r\n"), WiFi.localIP().toString().c_str()); }
    if(serialPort) { serialPort->printf_P(PSTR("  GW: %s\r\n"), WiFi.gatewayIP().toString().c_str()); }
    if(serialPort) { serialPort->printf_P(PSTR("  SNM: %s\r\n"), WiFi.subnetMask().toString().c_str()); }
  }
  else {
    return false;
  }
  if(serialPort) { serialPort->printf_P(PSTR("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); }
  usedInterface = interface;

  // Set time via NTP, as required for x.509 validation.
  yield();
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
  if(serialPort) { serialPort->printf_P(PSTR("\r\n%sCurrent UTC time: %s"), NTP_PREFIX, asctime(&timeinfo)); }
  if(serialPort) { serialPort->printf_P(PSTR("%sUTC ISO format: %s\r\n"), NTP_PREFIX, getISODateTime().c_str()); }

  // Setup MQTT topics.
  const int32_t clientNameSize = snprintf_P(mqttCredentials.clientName, sizeof(mqttCredentials.clientName), "%s_%s", DEVICE_TYPE, macAddress);
  const int32_t senderTopicSize = snprintf_P(mqttCredentials.senderTopic, sizeof(mqttCredentials.senderTopic), "%s/%s/%s", BASE_TOPIC, SENDER_TOPIC, macAddress);
  const int32_t receiverTopicSize = snprintf_P(mqttCredentials.receiverTopic, sizeof(mqttCredentials.receiverTopic), "%s/%s/%s", BASE_TOPIC, RECEIVER_TOPIC, macAddress);
  const bool clientNameValid = (clientNameSize >= 0 && clientNameSize < static_cast<int32_t>(sizeof(mqttCredentials.clientName)));
  const bool senderTopicValid = (senderTopicSize >= 0 && senderTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.senderTopic)));
  const bool receiverTopicValid = (receiverTopicSize >= 0 && receiverTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.receiverTopic)));
  if(serialPort) { serialPort->printf_P(PSTR("%sClient name:%s\r\n"), MQTT_PREFIX, clientNameValid ? OK_STATE : ERR_STATE); }
  if(serialPort) { serialPort->printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.clientName, clientNameSize); }
  if(serialPort) { serialPort->printf_P(PSTR("%sSender topic:%s\r\n"), MQTT_PREFIX, senderTopicValid ? OK_STATE : ERR_STATE); }
  if(serialPort) { serialPort->printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.senderTopic, senderTopicSize); }
  if(serialPort) { serialPort->printf_P(PSTR("%sReceiver topic:%s\r\n"), MQTT_PREFIX, receiverTopicValid ? OK_STATE : ERR_STATE); }
  if(serialPort) { serialPort->printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.receiverTopic, receiverTopicSize); }
  if(!clientNameValid) { return false; }
  if(!senderTopicValid) { return false; }
  if(!receiverTopicValid) { return false; }

  if(!checkFiles()) { return false; }
  if(!loadConfig(ConfigFile::NORMAL)) { return false; }
  if(!connect(CertFile::NORMAL)) { return false; }

  mqttClient.setCallback([this](const char* topic, uint8_t* payload, uint32_t length) { receiveMqttMessage(topic, payload, length); });
  MqttComBase::setMqttSender([this](const char* subTopic, const char* payload) { sendMqttMessage(subTopic, payload); });
  return true;
}

bool Connectivity::startWifi() {
  const bool wifiFileExists = LittleFS.exists(FPSTR(wifiFileLocation));
  if(serialPort) { serialPort->printf_P(PSTR("%sCheck wifi config:\r\n"), FS_PREFIX); }
  if(serialPort) { serialPort->printf_P(PSTR("  %s ->%s\r\n"), wifiFileLocation, wifiFileExists ? OK_STATE : ERR_STATE); }
  if(!wifiFileExists) { return false; }

  File wifiFile = LittleFS.open(FPSTR(wifiFileLocation), "r");
  if(serialPort) { serialPort->printf_P(PSTR("%sOpening: %s%s\r\n"), FS_PREFIX, wifiFile.fullName(), wifiFile ? OK_STATE : ERR_STATE); }
  if(!wifiFile) { return false; }

  StaticJsonDocument<256> wifiJson;
  DeserializationError deserializationError = deserializeJson(wifiJson, wifiFile);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(deSerResult) {
    const char* ssid = wifiJson["ssid"];
    const char* pass = wifiJson["password"];
    const uint8_t channel = wifiJson["channel"];
    JsonArray bssidArray = wifiJson["bssid"];
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
  if(serialPort) { serialPort->printf_P(PSTR("%sCheck config files:\r\n"), FS_PREFIX); }
  if(serialPort) { serialPort->printf_P(PSTR("  %s ->%s\r\n"), configFileLocation, configFileExists ? OK_STATE : ERR_STATE); }
  if(serialPort) { serialPort->printf_P(PSTR("  %s ->%s\r\n"), configBackupFileLocation, configBackupFileExists ? OK_STATE : ERR_STATE); }

  // Check for cert.
  const bool certFileExists = LittleFS.exists(FPSTR(certFileLocation));
  const bool certBackupFileExists = LittleFS.exists(FPSTR(certBackupFileLocation));
  if(serialPort) { serialPort->printf_P(PSTR("%sCheck certification files:\r\n"), FS_PREFIX); }
  if(serialPort) { serialPort->printf_P(PSTR("  %s ->%s\r\n"), certFileLocation, certFileExists ? OK_STATE : ERR_STATE); }
  if(serialPort) { serialPort->printf_P(PSTR("  %s ->%s\r\n"), certBackupFileLocation, certBackupFileExists ? OK_STATE : ERR_STATE); }

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
  if(!configFile) { return false; }

  StaticJsonDocument<256> configJson;
  DeserializationError deserializationError = deserializeJson(configJson, configFile);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(deSerResult) {
    strlcpy(mqttCredentials.userName, configJson["mqttUserName"], sizeof(mqttCredentials.userName));
    strlcpy(mqttCredentials.password, configJson["mqttPassword"], sizeof(mqttCredentials.password));
    strlcpy(mqttCredentials.serverName, configJson["mqttServerName"], sizeof(mqttCredentials.serverName));
    mqttCredentials.serverPort = configJson["mqttServerPort"];
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
  if(!certFile) { return false; }

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
  mqttClient.setServer(mqttCredentials.serverName, mqttCredentials.serverPort);
  const bool mqttConResult = mqttClient.connect(mqttCredentials.clientName, mqttCredentials.userName, mqttCredentials.password);
  if(serialPort) { serialPort->printf_P(PSTR("%sConnecting to MQTT broker:%s State: %d\r\n"), MQTT_PREFIX, mqttConResult ? OK_STATE : ERR_STATE, mqttClient.state()); }
  if(!mqttConResult) { return false; }
  const bool subResult = mqttClient.subscribe(mqttCredentials.receiverTopic, 1);
  if(serialPort) { serialPort->printf_P(PSTR("%sListening on: %s%s\r\n"), MQTT_PREFIX, mqttCredentials.receiverTopic, subResult ? OK_STATE : ERR_STATE); }
  if(!subResult) { return false; }

  return true;
}

bool Connectivity::loop() {
  static wl_status_t actualInterfaceStatus = WL_DISCONNECTED;
  const char* intPrefix;
  if(usedInterface == Interface::ETHERNET) { actualInterfaceStatus = ethInt.status(); intPrefix = ETH_PREFIX; }
  else if(usedInterface == Interface::WIFI) { actualInterfaceStatus = WiFi.status();  intPrefix = WIFI_PREFIX; }
  else { return false; }
  if(interfaceStatus != actualInterfaceStatus) {
    if(serialPort) { serialPort->printf_P(PSTR("%sStatus changed: %hd -> %hd\r\n"), intPrefix, interfaceStatus, actualInterfaceStatus); }
    interfaceStatus = actualInterfaceStatus;
    if(interfaceStatus != WL_CONNECTED) { MqttComBase::setConState(false); }
  }
  if(interfaceStatus != WL_CONNECTED) { return false; }

  const int8_t actualMqttState = mqttClient.state();
  if(mqttState != actualMqttState) {
    if(serialPort) { serialPort->printf_P(PSTR("%sStatus changed: %hd -> %hd\r\n"), MQTT_PREFIX, mqttState, actualMqttState); }
    mqttState = actualMqttState;
    if(mqttState == MQTT_CONNECTED) { MqttComBase::setConState(true); }
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
  const char* classID = nullptr;
  {
    StaticJsonDocument<MQTT_MAX_PACKET_SIZE> mqttMessageJson;
    DeserializationError deserializationError = deserializeJson(mqttMessageJson, payload, length);
    const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
    if(deSerResult) { classID = mqttMessageJson["classID"] | "Unknown"; }
    if(serialPort) { serialPort->printf_P(PSTR("%sSerialize received MQTT message:%s\r\n"), JSON_PREFIX, deSerResult ? OK_STATE : ERR_STATE); }
    if(!deSerResult) { return; }
  }
  if(!classID) { return; }
  for(uint8_t i = 0; messageMap[i] != nullptr; ++i) {
    const MqttComBase* currentObject = Connectivity::messageMap[i];
    if (currentObject != nullptr && strcmp(currentObject->getClassId(), classID) == 0) {
      if(serialPort) { serialPort->printf_P(PSTR("%sForward message to class with ID: %s\r\n"), MQTT_PREFIX, currentObject->getClassId()); }
      currentObject->messageReceived(payload, length);
      return;
    }
  }
  if(serialPort) { serialPort->printf_P(PSTR("%sNo class with ID: %s\r\n"), MQTT_PREFIX, classID); }
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
/*
bool Connectivity::registerCallback(MqttComBase* obj) {
  if(!obj) { return false; }
  if(messageMapPointer >= sizeof(messageMap)) { return false; }
  messageMap[messageMapPointer] = obj;
  messageMapPointer++;
  return true;
}
*/
