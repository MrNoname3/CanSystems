#ifndef CONNECTIVITY_HPP
#define CONNECTIVITY_HPP

#include <Arduino.h>                          /// Arduino libraries header.
#include <HardwareSerial.h>
#include <ESP8266WiFi.h>                      /// Wifi driver.
#include <ENC28J60lwIP.h>                     /// Ethernet driver.
#include <WiFiClientSecure.h>                 /// TCP client with SSL.
#include <PubSubClient.h>                     /// MQTT client.
#include <LittleFS.h>                         /// Use FLASH filesystem.
#include <ArduinoJson.h>                      /// Handle JSON files.

class Connectivity {

public:

  enum class Interface : uint8_t {
    WIFI = 0,
    ETHERNET
  };

  enum class ConfigFile : uint8_t {
    NORMAL = 0,
    BACKUP
  };
  
  enum class CertFile : uint8_t {
    NORMAL = 0,
    BACKUP
  };

  Connectivity(HardwareSerial* serial = nullptr, const uint8_t ethCS = D8) : 
    serialPort(serial), ethInt(ethCS), tcpClient(), mqttClient(tcpClient) {}

  /// @brief Destructor of the object.
  virtual ~Connectivity() = default;

  bool begin(Interface interface = Interface::ETHERNET) {
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

    // Setup MQTT topics.
    const int32_t clientNameSize = snprintf_P(mqttCredentials.clientName, sizeof(mqttCredentials.clientName), "%s_%s", DEVICE_TOPIC, macAddress);
    const int32_t senderTopicSize = snprintf_P(mqttCredentials.senderTopic, sizeof(mqttCredentials.senderTopic), "%s/%s/%s/%s", BASE_TOPIC, DEVICE_TOPIC, macAddress, SENDER_TOPIC);
    const int32_t receiverTopicSize = snprintf_P(mqttCredentials.receiverTopic, sizeof(mqttCredentials.receiverTopic), "%s/%s/%s/%s", BASE_TOPIC, DEVICE_TOPIC, macAddress, RECEIVER_TOPIC);
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

    // Check files.
    if(!checkFiles()) { return false; }
    if(!loadConfig(ConfigFile::NORMAL)) { return false; }
    if(!connect(CertFile::NORMAL)) { return false; }
    return true;
  }

  bool startWifi() {
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

  bool checkFiles() {
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

  bool loadConfig(ConfigFile actualConfig = ConfigFile::NORMAL) {
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

  bool connect(CertFile actualCert = CertFile::NORMAL) {
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
    const bool tcpConResult = tcpClient.connect(mqttCredentials.serverName, mqttCredentials.serverPort);
    if(serialPort) { serialPort->printf_P(PSTR("%sConnecting to: %s:%hu%s\r\n"), TCP_PREFIX, mqttCredentials.serverName, mqttCredentials.serverPort, tcpConResult ? OK_STATE : ERR_STATE); }
    if(!tcpConResult) { return false; }

    // MQTT connection.
    mqttClient.setServer(mqttCredentials.serverName, mqttCredentials.serverPort);
    const bool mqttConResult = mqttClient.connect(mqttCredentials.clientName, mqttCredentials.userName, mqttCredentials.password);
    if(serialPort) { serialPort->printf_P(PSTR("%sConnecting to MQTT broker:%s State: %d\r\n"), MQTT_PREFIX, mqttConResult ? OK_STATE : ERR_STATE, mqttClient.state()); }
    if(!mqttConResult) { return false; }

    return true;
  }

  bool loop() {
    return mqttClient.loop();
  }

  Connectivity(const Connectivity&) = delete;                       // Define copy constructor.
  Connectivity& operator=(const Connectivity&) = delete;            // Define copy assignment operator.
  Connectivity(Connectivity&&) = delete;                            // Define move constructor.
  Connectivity& operator=(Connectivity&&) = delete;                 // Define move assignment operator.

private:

  struct __attribute__((packed))
  MqttCredentials {
    char userName[24];
    char password[24];
    char serverName[34];
    uint16_t serverPort;
    char clientName[32];
    char senderTopic[48];
    char receiverTopic[48];
    MqttCredentials() : userName{'\0'}, password{'\0'}, serverName{'\0'}, serverPort(0), clientName{'\0'}, senderTopic{'\0'}, receiverTopic{'\0'} {}
  };

  Stream* serialPort;
  ENC28J60lwIP ethInt;
  WiFiClientSecure tcpClient;
  PubSubClient mqttClient;

  MqttCredentials mqttCredentials;

  static const char PROGMEM wifiFileLocation[];
  static const char PROGMEM configFileLocation[];
  static const char PROGMEM configBackupFileLocation[];
  static const char PROGMEM certFileLocation[];
  static const char PROGMEM certBackupFileLocation[];

  static const char PROGMEM BASE_TOPIC[];
  static const char PROGMEM SENDER_TOPIC[];
  static const char PROGMEM RECEIVER_TOPIC[];

public:
  static const char PROGMEM OK_STATE[];
  static const char PROGMEM ERR_STATE[];
  static const char PROGMEM DEVICE_TOPIC[];

private:
  static const char PROGMEM INIT_PREFIX[];
  static const char PROGMEM FS_PREFIX[];
  static const char PROGMEM ETH_PREFIX[];
  static const char PROGMEM WIFI_PREFIX[];
  static const char PROGMEM NTP_PREFIX[];
  static const char PROGMEM JSON_PREFIX[];
  static const char PROGMEM TCP_PREFIX[];
  static const char PROGMEM MQTT_PREFIX[];

};

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

#endif