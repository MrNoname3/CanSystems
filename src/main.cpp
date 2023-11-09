#include "main.hpp"

const char sw_ver[] = "V2.0.3";
const char hw_ver[] = "V2.0.0";
const uint8_t LED    = D8;
const uint8_t SPI_CS = D0;
const uint8_t RAD    = D2;                               //D5 GPIO14   A sugarzasmero pinje.
const uint8_t measure_time = 60;  //sec
uint32_t cpm = 0;
const uint8_t topic_name_size = 50;
char mqtt_cpm[topic_name_size] = { '\0' };
char mqtt_log[topic_name_size] = { '\0' };
char mqtt_cmd[topic_name_size] = { '\0' };
char mqtt_wol[topic_name_size] = { '\0' };
char mqtt_client_name[topic_name_size] = { '\0' };

//////////////////////////////////////////////////
bool wol_state = false;
char WOL_MAC[mac_size] = { '\0' };

WiFiUDP UDP;
WakeOnLan WOL(UDP);
//////////////////////////////////////////////////

WiFiClientSecure tcp_client;
ENC28J60lwIP eth(SPI_CS);
PubSubClient mqtt(tcp_client);
Ticker ticker;

ESP8266WebServer httpServer(28080);
ESP8266HTTPUpdateServer httpUpdater;

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(RAD, INPUT);  
  delay(1);
  Serial.println("***************************************");
  Serial.printf("[%lu] Program starting...\r\n", millis());
  ticker.attach(0.2, tick);  

  Serial.printf("[%lu] Initialising ethernet: ", millis());
  eth.setDefault();         // default route set through this interface
  WiFi.mode(WIFI_OFF);
  uint8_t mac[6];    
  wifi_get_macaddr(STATION_IF, mac);
  sprintf(MAC_Address, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  sprintf(mqtt_cpm, "%s/%s/%s", mqtt_base_topic, MAC_Address, mqtt_pub_cpm);
  sprintf(mqtt_log, "%s/%s/%s", mqtt_base_topic, MAC_Address, mqtt_pub_log);
  sprintf(mqtt_cmd, "%s/%s/%s", mqtt_base_topic, MAC_Address, mqtt_sub_cmd);
  sprintf(mqtt_wol, "%s/%s/%s", mqtt_base_topic, MAC_Address, mqtt_sub_wol);
  sprintf(mqtt_client_name, "%s_%s", mqtt_client, MAC_Address);

  if (!eth.begin(mac)) {
    Serial.println(ERROR_state);    
    RestartESP();
  }
  else {
    Serial.println(OK_state);    
  }

  Serial.printf("[%lu] Connecting to router", millis());
  while (!eth.connected()) {
    Serial.print(".");
    delay(500);
  }

  ////////////////////////////////////////
  yield();
  String public_ip = "000.000.000.000";
  ////////////////////////////////////////

  if(eth.connected() == true) {
    Serial.printf(" %s\r\n", OK_state);
    ////////////////////////////////////////
    WiFiClient client;
    client.setTimeout(10000);
    Serial.printf("[%lu] Getting public IP: ", millis());
    client.connect("ifconfig.me", 80);
    
    yield();
    HTTPClient http;    
    http.begin(client, "http://ifconfig.me/ip");
    int16_t httpCode = http.GET();
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        public_ip = http.getString();      
      } 
    }
    http.end();
    client.stopAll();

    if(public_ip != "000.000.000.000") {
      Serial.println(OK_state);
    }
    else {
      Serial.println(ERROR_state);
      RestartESP();
    }
    ////////////////////////////////////////
    Serial.printf("IP_P: %s\r\n", public_ip.c_str());
    Serial.printf("IP_L: %s\r\n", eth.localIP().toString().c_str());
    Serial.printf("GW: %s\r\n", eth.gatewayIP().toString().c_str());
    Serial.printf("NM: %s\r\n", eth.subnetMask().toString().c_str());
    Serial.printf("MAC: %s\r\n", MAC_Address);
  }
  else {
    Serial.printf(" %s\r\n", ERROR_state);
  }

  yield();
  setClock();
  DNS_Resolv(host);

  yield();
  X509List cert(CACertificate);
  tcp_client.setTrustAnchors(&cert);
  tcp_client.setTimeout(10000);

  Serial.printf("[%lu] Connecting to server: ", millis());
  if ( tcp_client.connect(host, mqtt_port) == true ) {
    Serial.println(OK_state);
  }
  else {
    Serial.println(ERROR_state);
  }

  yield();
  Serial.printf("[%lu] Connecting to MQTT broker: ", millis());
  if (mqtt.connect(mqtt_client_name, mqtt_user, mqtt_pass) == true) {                      //Soros Debug.
    Serial.println(OK_state);
  }
  else {
    Serial.printf("ERROR State: %d\r\n", mqtt.state()); 
  }

  Serial.printf("[%lu] Software version: %s\r\n", millis(), sw_ver);
  Serial.printf("[%lu] Hardware version: %s\r\n", millis(), hw_ver);
  Serial.printf("[%lu] Internal VCC: %1.2fV\r\n", millis(), readVoltage());
  
  yield();
  httpUpdater.setup(&httpServer);
  httpUpdater.updateCredentials(mqtt_user, update_passwd);
  httpServer.begin();

  char system_info_json[320] = { '\0' };

  sprintf(system_info_json, json_variables, 
    public_ip.c_str(),
    eth.localIP().toString().c_str(),
    eth.gatewayIP().toString().c_str(),
    eth.subnetMask().toString().c_str(),
    MAC_Address,
    sw_ver,
    hw_ver,  
    readVoltage(),
    getClock()
  );

  yield();
  mqtt.publish(mqtt_log, system_info_json);
  mqtt.setCallback(onMqttPublish);
  mqtt.subscribe(mqtt_cmd);
  mqtt.subscribe(mqtt_wol);

  //////////////////////////////////////////////////
  // Optional  => To calculate the broadcast address, otherwise 255.255.255.255 is used (which is denied in some networks).
  WOL.calculateBroadcastAddress(eth.localIP(), eth.subnetMask());
  //////////////////////////////////////////////////

  attachInterrupt(RAD, Counter, FALLING);
  ticker.detach();
  LED_L;  
  wdt_disable();
  yield();
  cpm = 0;
}

void loop() {  
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
    DNS_Resolv(host);
  }

  httpServer.handleClient();                    //Sample update URL: http://192.168.51.120:28080/update

  if( wol_state == true ) {   
    const uint16_t wol_time = 200;
    const uint8_t wol_count = 3;    
    static uint32_t wol_timer = 0;
    static uint8_t wol_counter = 0;

    if( millis() - wol_timer >= wol_time ) {
      wol_timer = millis();
      yield();
      Serial.printf("[%lu] Sending WOL to \"%s\" -> ", millis(), WOL_MAC);
      if( WOL.sendMagicPacket(WOL_MAC) == true ) { // Send Wake On Lan packet with the above MAC address. Default to port 9.
        Serial.println(OK_state);
      }
      else {
        Serial.println(ERROR_state);
      }
      
      wol_counter++;
      if( wol_counter >= wol_count ) {
        wol_counter = 0;
        wol_state = false;
      }
    }
  }

}

IPAddress DNS_Resolv(const char* host_p) {
  Serial.printf("[%lu] Resolving DNS: ", millis());
  IPAddress remote_addr;
  if (WiFi.hostByName(host_p, remote_addr, 1000)) {
    Serial.println(OK_state);
  }
  else {
    Serial.println(ERROR_state);
  }
  return remote_addr;
}

void ConnectionStatus( void ) {
  const uint16_t checking_time = 100;
  static uint32_t checking_timer = 0; 

  static bool tcp_status_old = false;
  static bool mqtt_status_old = false;

  if( millis() - checking_timer >= checking_time ) {
    checking_timer = millis();
    
    bool tcp_status = tcp_client.connected();
    bool mqtt_status = mqtt.connected();    

    if ((tcp_status == 1) && (tcp_status_old == false)) {
      Serial.printf("[%lu] Webshocket: %s\r\n", millis(), OK_state);
      tcp_status_old = true;
      cpm = 0;
    }
    else if ((tcp_status == 0) && (tcp_status_old == true)) {
      Serial.printf("[%lu] Webshocket: %s\r\n", millis(), ERROR_state);
      tcp_status_old = false;  
      cpm = 0;    
    }

    if ((mqtt_status == 1) && (mqtt_status_old == false)) {
      Serial.printf("[%lu] MQTT: %s\r\n", millis(), OK_state);
      mqtt_status_old = true;
      cpm = 0; 
    }
    else if ((mqtt_status == 0) && (mqtt_status_old == true)) {
      Serial.printf("[%lu] MQTT: %s\r\n", millis(), ERROR_state);
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

  if( strcmp(topic, mqtt_cmd) == 0 ) {
    if( strcmp(data_buff, "REBOOT") == 0 ) {       
      RestartESP();
      return;
    }    
  }

  if( strcmp(topic, mqtt_wol) == 0 ) {
    sprintf(WOL_MAC, data_buff);
    wol_state = true;
  }


}


// Set time via NTP, as required for x.509 validation
void setClock(void) {  
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
      Serial.printf(" %s\r\n", ERROR_state);   
      RestartESP();
      return;
    }
  }

  Serial.printf(" %s\r\n", OK_state);  
  struct tm timeinfo;
  gmtime_r(&nowSecs, &timeinfo);
  Serial.printf("[%lu] Current time: ", millis());  
  Serial.print(asctime(&timeinfo));  
}

const char* getClock(void) {

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

void RestartESP(void) {
  Serial.println("Restarting ESP...");
  delay(1000);                                        // 1s-es delay.
  ESP.restart();                                      // ESP ujrainditasa.
  delay(10000);                                       // 10s-es delay, hogy a restart elott mar ne csinaljon semmit.
}

IRAM_ATTR void Counter(void) {                        // Radiation pin interrupt fuggvenye.
  cpm++;                                              // CPS valtozo novelese.
}

float readVoltage(void) {                             // Read internal VCC
  float volts = ESP.getVcc();  
  return (volts / 1000);
}

