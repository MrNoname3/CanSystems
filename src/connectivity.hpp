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

class MqttComBase;

class Connectivity {
public:
  enum class Interface : uint8_t {
    WIFI = 0,
    ETHERNET,
    UNKNOWN
  };

  enum class ConfigFile : uint8_t {
    NORMAL = 0,
    BACKUP
  };

  enum class CertFile : uint8_t {
    NORMAL = 0,
    BACKUP
  };

  Connectivity(HardwareSerial* serial = nullptr, const uint8_t ethCS = D8);

  /// @brief Destructor of the object.
  virtual ~Connectivity() = default;

  bool begin(Interface interface = Interface::ETHERNET);

  bool startWifi();

  bool checkFiles();

  bool loadConfig(ConfigFile actualConfig = ConfigFile::NORMAL);

  bool connect(CertFile actualCert = CertFile::NORMAL);

  bool loop();

  void receiveMqttMessage(const char* topic, uint8_t* payload, uint32_t length);

  void sendMqttMessage(const char* subTopic, const char* payload);

  String getISODateTime();

  //static bool registerCallback(MqttComBase* obj);

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
  Interface usedInterface;
  wl_status_t interfaceStatus;
  MqttCredentials mqttCredentials;
  int8_t mqttState;

  //static MqttComBase* messageMap[10];
  //static uint8_t messageMapPointer;

public:
  static const MqttComBase* messageMap[];
private:
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

#endif