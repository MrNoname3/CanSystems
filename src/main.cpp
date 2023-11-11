#include "main.hpp"

//--- Variables ---//
const uint8_t measure_time = 60;  //sec
uint32_t cpm = 0;

//--- Networking ---//
WiFiClientSecure tcp_client;
ENC28J60lwIP eth(SPI_CS);
PubSubClient mqtt(tcp_client);

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
  Serial.println(F("**************************************************"));
  Serial.println(F("Starting..."));
  Serial.print(F("[INIT] CPP: "));
  Serial.println(__cplusplus);
  Serial.print(F("[INIT] FW: "));
  Serial.println(SW_VERSION);
  Serial.print(F("[INIT] Git hash: "));
  Serial.println(F(GIT_COMMIT_HASH));
  Serial.printf("[INIT] Internal VCC: %humV\r\n", ESP.getVcc());

  WiFi.mode(WIFI_OFF);
  eth.setDefault();         // default route set through this interface
  uint8_t mac[6] = { 0 };
  char macAddress[13] = { '\0' };
  wifi_get_macaddr(STATION_IF, mac);
  sprintf(macAddress, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  Serial.print(F("[ETH] Initialising ethernet modul:"));
  if(!eth.begin(mac)) {
    Serial.println(ERR_STATE);
  }
  else {
    Serial.println(OK_STATE);
  }

  Serial.print(F("[ETH] Connecting to router"));
  while (!eth.connected()) {
    Serial.print(".");
    delay(500);
  }

  if(eth.connected() == true) {
    Serial.println(OK_STATE);
    Serial.printf("  IP: %s\r\n", eth.localIP().toString().c_str());
    Serial.printf("  GW: %s\r\n", eth.gatewayIP().toString().c_str());
    Serial.printf("  SNM: %s\r\n", eth.subnetMask().toString().c_str());
    Serial.printf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    wdt_reset();
  }
  else {
    Serial.println(ERR_STATE);
  }

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
  Serial.printf("\r\n[NTP] Current UTC time: %s", asctime(&timeinfo));

  //DNS_Resolv(mqttHost);

  X509List cert(CACertificate);
  tcp_client.setTrustAnchors(&cert);
  tcp_client.setTimeout(5000);

  Serial.print("[TCP] Connecting to server:");
  if(tcp_client.connect(mqttHost, mqttPort) == true) {
    Serial.println(OK_STATE);
  }
  else {
    Serial.println(ERR_STATE);
  }

  // Setup MQTT topics.
  constexpr const char base[]     = "iot";
  constexpr const char device[]   = "test";
  constexpr const char sender[]   = "dtos";
  constexpr const char receiver[] = "stod";

  Serial.print(F("[MQTT] Client name:"));
  constexpr const uint8_t clientNameMaxSize = sizeof(device) + sizeof(macAddress);
  char clientName[clientNameMaxSize] = { '\0' };
  int32_t clientNameSize = snprintf(clientName, clientNameMaxSize, "%s_%s", device, macAddress);
  if(clientNameSize >= 0 && clientNameSize < clientNameMaxSize) {
    Serial.println(OK_STATE);
  }
  else {
    Serial.println(ERR_STATE);
  }
  Serial.printf("  %s Length: %d\r\n", clientName, clientNameSize);

  Serial.print(F("[MQTT] Sender topic(s):"));
  constexpr const uint8_t senderTopicMaxSize = sizeof(base) + sizeof(device) + sizeof(macAddress) + sizeof(sender);
  char senderTopic[senderTopicMaxSize] = { '\0' };
  int32_t senderTopicSize = snprintf(senderTopic, senderTopicMaxSize, "%s/%s/%s/%s", base, device, macAddress, sender);
  if(senderTopicSize >= 0 && senderTopicSize < senderTopicMaxSize) {
    Serial.println(OK_STATE);
  }
  else {
    Serial.println(ERR_STATE);
  }
  Serial.printf("  %s Length: %d\r\n", senderTopic, senderTopicSize);

  Serial.print(F("[MQTT] Receiver topic(s):"));
  constexpr const uint8_t receiverTopicMaxSize = sizeof(base) + sizeof(device) + sizeof(macAddress) + sizeof(receiver);
  char receiverTopic[receiverTopicMaxSize] = { '\0' };
  int32_t receiverTopicSize = snprintf(receiverTopic, receiverTopicMaxSize, "%s/%s/%s/%s", base, device, macAddress, receiver);
  if(receiverTopicSize >= 0 && receiverTopicSize < receiverTopicMaxSize) {
    Serial.println(OK_STATE);
  }
  else {
    Serial.println(ERR_STATE);
  }
  Serial.printf("  %s Length: %d\r\n", receiverTopic, receiverTopicSize);

  Serial.printf("[MQTT] Connecting to MQTT broker:");
  if(mqtt.connect(clientName, mqtt_user, mqtt_pass) == true) {
    Serial.println(OK_STATE);
  }
  else {
    Serial.printf("ERROR State: %d\r\n", mqtt.state());
  }

  mqtt.setCallback(onMqttPublish);
  mqtt.subscribe(receiverTopic);

  attachInterrupt(RAD, Counter, FALLING);

  Serial.println(F("**************************************************"));
  Serial.println(F("Loop starting..."));
  ticker.detach();
  wdt_reset();
  LED_L;
}

void loop() {
  /*
  static uint32_t measure_timer = millis();
  static uint32_t dns_timer = millis();

  yield();
  ConnectionStatus();

  if( millis() - measure_timer >= measure_time * 1000 ) {
    measure_timer = millis();
    Serial.printf("[%lu] CPM: %u\r\n", millis(), cpm);
    char cpm_str[12] = { '\0' };
    sprintf(cpm_str, "%u", cpm);
    mqtt.publish(mqtt_cpm, cpm_str);
    cpm = 0;
  }

  yield();
  wdt_reset();
  mqtt.loop();

  if( millis() - dns_timer >= 6 * 60 * 60 * 1000 ) {
    dns_timer = millis();
    yield();
    DNS_Resolv(mqttHost);
  }
*/
  static uint32_t timer = millis();

  if(millis() - timer >= 200) {
    static bool once = true;
    if(!mqtt.connected() && once) {
      once = false;
      Serial.println("TCP connection lost!");
    }
    timer = millis();

  }

  mqtt.loop();
  wdt_reset();
}

IPAddress DNS_Resolv(const char* host_p) {
  Serial.printf("[%lu] Resolving DNS: ", millis());
  IPAddress remote_addr;
  if (WiFi.hostByName(host_p, remote_addr, 1000)) {
    Serial.println(OK_STATE);
  }
  else {
    Serial.println(ERR_STATE);
  }
  return remote_addr;
}

void ConnectionStatus() {
  const uint16_t checking_time = 100;
  static uint32_t checking_timer = 0;

  static bool tcp_status_old = false;
  static bool mqtt_status_old = false;

  if( millis() - checking_timer >= checking_time ) {
    checking_timer = millis();

    bool tcp_status = tcp_client.connected();
    bool mqtt_status = mqtt.connected();

    if ((tcp_status == 1) && (tcp_status_old == false)) {
      Serial.printf("[%lu] Webshocket: %s\r\n", millis(), OK_STATE);
      tcp_status_old = true;
      cpm = 0;
    }
    else if ((tcp_status == 0) && (tcp_status_old == true)) {
      Serial.printf("[%lu] Webshocket: %s\r\n", millis(), ERR_STATE);
      tcp_status_old = false;
      cpm = 0;
    }

    if ((mqtt_status == 1) && (mqtt_status_old == false)) {
      Serial.printf("[%lu] MQTT: %s\r\n", millis(), OK_STATE);
      mqtt_status_old = true;
      cpm = 0;
    }
    else if ((mqtt_status == 0) && (mqtt_status_old == true)) {
      Serial.printf("[%lu] MQTT: %s\r\n", millis(), ERR_STATE);
      mqtt_status_old = false;
      cpm = 0;
    }

    if( (tcp_status_old  == false) || (mqtt_status_old == false) ) {
      RestartESP();
    }

  }
}


void onMqttPublish(const char* topic, uint8_t* payload, int length) {

  const uint8_t payload_size = 30;
  char data_buff[payload_size] = { '\0' };

  Serial.printf("[%lu] Received: %s %.*s\n", millis(), topic, length, payload);

  if( length >= payload_size ) {
    Serial.println("Payload is bigger than buffer size!");
    return;
  }
  else {
    memcpy(data_buff, payload, length);
  }


}


// Set time via NTP, as required for x.509 validation
void setClock() {
  configTime(2 * 3600, 0, "0.hu.pool.ntp.org", "1.hu.pool.ntp.org", "2.hu.pool.ntp.org");

  Serial.printf("[%lu] Waiting for NTP time sync", millis());
  time_t nowSecs = time(nullptr);

  uint8_t timeout = 0;

  while (nowSecs < 8 * 3600 * 2) {
    delay(500);
    Serial.print(F("."));
    yield();
    nowSecs = time(nullptr);
    timeout++;
    if (timeout >= 20) {
      timeout = 0;
      Serial.printf(" %s\r\n", ERR_STATE);
      RestartESP();
      return;
    }
  }

  Serial.printf(" %s\r\n", OK_STATE);
  struct tm timeinfo;
  gmtime_r(&nowSecs, &timeinfo);
  Serial.printf("[%lu] Current time: ", millis());
  Serial.print(asctime(&timeinfo));
}

const char* getClock() {

  const uint8_t time_array_size = 75;

  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  static char actualTime[time_array_size] = {'\0'};
  memset(actualTime, 0, sizeof(actualTime));

  sprintf(actualTime, "%04d.%02d.%02d. %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

  return actualTime;
}

void tick() {                                         // Toggle state
  LED_T;                                              // Set pin to the opposite state
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
