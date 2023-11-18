#include "main.hpp"

//--- Variables ---//
const uint8_t measure_time = 60;  //sec
uint32_t cpm = 0;

//--- Networking ---//
WiFiClientSecure tcpClient;
ENC28J60lwIP eth(SPI_CS);
PubSubClient mqtt(tcpClient);
MqttCredentials mqttCredentials;

//--- Debug LED ---//
Ticker ticker;

void setup() {
  wdt_disable();                                          // Disables the SW watchdog and enables the HW watchdog -> ~8400ms
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(RAD, INPUT);
  delay(1);
  ticker.attach(0.2, tick);
  Serial.println();
  Serial.println(F("******************************************************"));
  Serial.println(F("Starting..."));
  Serial.print(F("[INIT] CPP: "));
  Serial.println(__cplusplus);
  Serial.print(F("[INIT] FW: "));
  Serial.println(FPSTR(SW_VERSION));
  Serial.print(F("[INIT] Git hash: "));
  Serial.println(F(GIT_COMMIT_HASH));
  Serial.printf_P(PSTR("[INIT] Internal VCC: %humV\r\n"), ESP.getVcc());

  Serial.print(F("[FS] Initialising filesystem:"));
  LittleFS.begin() ? Serial.println(FPSTR(OK_STATE)) : Serial.println(FPSTR(ERR_STATE));

  WiFi.mode(WIFI_OFF);
  eth.setDefault();         // default route set through this interface
  uint8_t mac[6] = { 0 };
  char macAddress[13] = { '\0' };
  wifi_get_macaddr(STATION_IF, mac);
  sprintf(macAddress, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  Serial.print(F("[ETH] Initialising ethernet modul:"));
  if(!eth.begin(mac)) {
    Serial.println(FPSTR(ERR_STATE));
  }
  else {
    Serial.println(FPSTR(OK_STATE));
  }

  Serial.print(F("[ETH] Connecting to router"));
  while (!eth.connected()) {
    Serial.print(F("."));
    delay(500);
  }

  if(eth.connected() == true) {
    Serial.println(FPSTR(OK_STATE));
    Serial.printf_P(PSTR("  IP: %s\r\n"), eth.localIP().toString().c_str());
    Serial.printf_P(PSTR("  GW: %s\r\n"), eth.gatewayIP().toString().c_str());
    Serial.printf_P(PSTR("  SNM: %s\r\n"), eth.subnetMask().toString().c_str());
    Serial.printf_P(PSTR("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    wdt_reset();
  }
  else {
    Serial.println(FPSTR(ERR_STATE));
  }

  // Set time via NTP, as required for x.509 validation.
  Serial.print(F("[NTP] Waiting for NTP time sync"));
  configTime(0, 0, "0.hu.pool.ntp.org", "1.hu.pool.ntp.org", "2.hu.pool.ntp.org");
  time_t nowSecs = time(nullptr);
  while(nowSecs < 8 * 3600 * 2) {
    delay(500);
    Serial.print(F("."));
    nowSecs = time(nullptr);
  }
  tm timeinfo;
  gmtime_r(&nowSecs, &timeinfo);
  Serial.printf_P(PSTR("\r\n[NTP] Current UTC time: %s"), asctime(&timeinfo));

  // Check for config.
  const bool configFileExists = LittleFS.exists(FPSTR(configFileLocation));
  const bool configBackupFileExists = LittleFS.exists(FPSTR(configBackupFileLocation));
  Serial.println(F("[FS] Check config files:"));
  Serial.printf_P(PSTR("  %s"), configFileLocation);
  configFileExists ? Serial.println(FPSTR(OK_STATE)) : Serial.println(FPSTR(ERR_STATE));
  Serial.printf_P(PSTR("  %s"), configBackupFileLocation);
  configBackupFileExists ? Serial.println(FPSTR(OK_STATE)) : Serial.println(FPSTR(ERR_STATE));

  Serial.printf_P(PSTR("[FS] Opening: %s"), configFileLocation);
  File configFile = LittleFS.open(FPSTR(configFileLocation), "r");
  configFile ? Serial.println(FPSTR(OK_STATE)) : Serial.println(FPSTR(ERR_STATE));

  Serial.print(F("[JSON] Serialize file:"));
  StaticJsonDocument<256> configJson;
  DeserializationError deserializationError = deserializeJson(configJson, configFile);
  if(deserializationError == DeserializationError::Code::Ok) {
    Serial.println(FPSTR(OK_STATE));
    strlcpy(mqttCredentials.userName, configJson["mqttUserName"], sizeof(mqttCredentials.userName));
    strlcpy(mqttCredentials.password, configJson["mqttPassword"], sizeof(mqttCredentials.password));
    strlcpy(mqttCredentials.serverName, configJson["mqttServerName"], sizeof(mqttCredentials.serverName));
    mqttCredentials.serverPort = configJson["mqttServerPort"];
  }
  else {
    Serial.println(FPSTR(ERR_STATE));
  }
  configFile.close();

  // Setup MQTT topics.
  Serial.print(F("[MQTT] Client name:"));
  int32_t clientNameSize = snprintf_P(mqttCredentials.clientName, sizeof(mqttCredentials.clientName), "%s_%s", deviceTopic, macAddress);
  if(clientNameSize >= 0 && clientNameSize < static_cast<int32_t>(sizeof(mqttCredentials.clientName))) {
    Serial.println(FPSTR(OK_STATE));
  }
  else {
    Serial.println(FPSTR(ERR_STATE));
  }
  Serial.printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.clientName, clientNameSize);

  Serial.print(F("[MQTT] Sender topic(s):"));
  int32_t senderTopicSize = snprintf_P(mqttCredentials.senderTopic, sizeof(mqttCredentials.senderTopic), "%s/%s/%s/%s", baseTopic, deviceTopic, macAddress, senderTopic);
  if(senderTopicSize >= 0 && senderTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.senderTopic))) {
    Serial.println(FPSTR(OK_STATE));
  }
  else {
    Serial.println(FPSTR(ERR_STATE));
  }
  Serial.printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.senderTopic, senderTopicSize);

  Serial.print(F("[MQTT] Receiver topic(s):"));
  int32_t receiverTopicSize = snprintf_P(mqttCredentials.receiverTopic, sizeof(mqttCredentials.receiverTopic), "%s/%s/%s/%s", baseTopic, deviceTopic, macAddress, receiverTopic);
  if(receiverTopicSize >= 0 && receiverTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.receiverTopic))) {
    Serial.println(FPSTR(OK_STATE));
  }
  else {
    Serial.println(FPSTR(ERR_STATE));
  }
  Serial.printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.receiverTopic, receiverTopicSize);

  connectToServer();

  mqtt.setCallback(onMqttPublish);
  mqtt.subscribe(mqttCredentials.receiverTopic);

  attachInterrupt(RAD, Counter, FALLING);

  Serial.println(F("******************************************************"));
  Serial.println(F("Loop starting..."));
  ticker.detach();
  wdt_reset();
  LED_L;
}

void loop() {
  if(eth.status() == 3) {
    if(!mqtt.loop()) {
      static int8_t lastMqttState = 10;
      const int8_t mqttState = mqtt.state();
      if(lastMqttState != mqttState) {
        Serial.print(F("MQTT status changed: "));
        lastMqttState = mqttState;
        switch(mqttState) {
          case MQTT_CONNECTION_TIMEOUT: {
            Serial.println(F("MQTT_CONNECTION_TIMEOUT"));
          } break;
          case MQTT_CONNECTION_LOST:{
            Serial.println(F("MQTT_CONNECTION_LOST"));
          } break;
          case MQTT_CONNECT_FAILED: {
            Serial.println(F("MQTT_CONNECT_FAILED"));
          } break;
          case MQTT_DISCONNECTED: {
            Serial.println(F("MQTT_DISCONNECTED"));
          } break;
          case MQTT_CONNECTED: {
            Serial.println(F("MQTT_CONNECTED"));
          } break;
          case MQTT_CONNECT_BAD_PROTOCOL: {
            Serial.println(F("MQTT_CONNECT_BAD_PROTOCOL"));
          } break;
          case MQTT_CONNECT_BAD_CLIENT_ID: {
            Serial.println(F("MQTT_CONNECT_BAD_CLIENT_ID"));
          } break;
          case MQTT_CONNECT_UNAVAILABLE: {
            Serial.println(F("MQTT_CONNECT_UNAVAILABLE"));
          } break;
          case MQTT_CONNECT_BAD_CREDENTIALS: {
            Serial.println(F("MQTT_CONNECT_BAD_CREDENTIALS"));
          } break;
          case MQTT_CONNECT_UNAUTHORIZED: {
            Serial.println(F("MQTT_CONNECT_UNAUTHORIZED"));
          } break;
        }
      }
      if(mqttState < 0) {
        static uint32_t reconnectTimer = millis();
        if(millis() - reconnectTimer >= 10000) {
          reconnectTimer = millis();
          Serial.printf("ETH status: %hu\r\n", static_cast<uint8_t>(eth.status()));
          connectToServer();
        }
      }
    }
  }

  /*
  static uint32_t measure_timer = millis();
  static uint32_t dns_timer = millis();

  if( millis() - measure_timer >= measure_time * 1000 ) {
    measure_timer = millis();
    Serial.printf("[%lu] CPM: %u\r\n", millis(), cpm);
    char cpm_str[12] = { '\0' };
    sprintf(cpm_str, "%u", cpm);
    mqtt.publish(mqtt_cpm, cpm_str);
    cpm = 0;
  }

  yield();
*/
/*
  static uint32_t timer = millis();

  if(millis() - timer >= 200) {
    static bool once = true;
    if(!mqtt.connected() && once) {
      once = false;
      Serial.println("TCP connection lost!");
    }
    timer = millis();
  }
*/

  yield();
  wdt_reset();
}

void onMqttPublish(const char* topic, uint8_t* payload, int length) {

  const uint8_t payload_size = 30;
  char data_buff[payload_size] = { '\0' };

  Serial.printf_P(PSTR("[%lu] Received: %s %.*s\n"), millis(), topic, length, payload);

  if( length >= payload_size ) {
    Serial.println(F("Payload is bigger than buffer size!"));
    return;
  }
  else {
    memcpy(data_buff, payload, length);
  }

}

void connectToServer() {
  // Check certificates.
  const bool certFileExists = LittleFS.exists(FPSTR(certFileLocation));
  const bool certBackupFileExists = LittleFS.exists(FPSTR(certBackupFileLocation));
  Serial.println(F("[FS] Check certification files:"));
  Serial.printf_P(PSTR("  %s"), certFileLocation);
  certFileExists ? Serial.println(FPSTR(OK_STATE)) : Serial.println(FPSTR(ERR_STATE));
  Serial.printf_P(PSTR("  %s"), certBackupFileLocation);
  certBackupFileExists ? Serial.println(FPSTR(OK_STATE)) : Serial.println(FPSTR(ERR_STATE));

  Serial.printf_P(PSTR("[FS] Opening: %s"), certFileLocation);
  File certFile = LittleFS.open(FPSTR(certFileLocation), "r");
  certFile ? Serial.println(FPSTR(OK_STATE)) : Serial.println(FPSTR(ERR_STATE));

  X509List cert(certFile);
  tcpClient.setTrustAnchors(&cert);
  tcpClient.setTimeout(5000);
  certFile.close();
  //tcpClient.getLastSSLError() == BR_ERR_OK ? 
  yield();

  Serial.printf_P(PSTR("[TCP] Connecting to: %s:%hu"), mqttCredentials.serverName, mqttCredentials.serverPort);
  if(tcpClient.connect(mqttCredentials.serverName, mqttCredentials.serverPort) == true) {
    Serial.println(FPSTR(OK_STATE));
  }
  else {
    Serial.println(FPSTR(ERR_STATE));
  }

  mqtt.setServer(mqttCredentials.serverName, mqttCredentials.serverPort);

  Serial.print("[MQTT] Connecting to MQTT broker:");
  if(mqtt.connect(mqttCredentials.clientName, mqttCredentials.userName, mqttCredentials.password) == true) {
    Serial.println(FPSTR(OK_STATE));
  }
  else {
    Serial.printf_P(PSTR(" [ ERR ] State: %d\r\n"), mqtt.state());
  }
}

void tick() {
  LED_T;                                              // Set pin to the opposite state.
}

void RestartESP() {
  Serial.println(F("Restarting..."));
  Serial.flush();                                     // Sends out data from serial buffer, before reset.
  ESP.restart();
  delay(10000);                                       // Prevent doing anything before restart.
}

IRAM_ATTR void Counter() {
  cpm++;
}
