#include "main.hpp"

//--- Variables ---//
const uint8_t measure_time = 60;  //sec
uint32_t cpm = 0;

//--- Networking ---//
const char Connectivity::DEVICE_TOPIC[] PROGMEM = "test";
Connectivity iotConn(&Serial, SPI_CS);

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

  const bool conResult = iotConn.begin(Connectivity::Interface::ETHERNET);
  Serial.printf_P(PSTR("IOT connection:%s\r\n"), conResult ? Connectivity::OK_STATE : Connectivity::ERR_STATE);

/*
  mqtt.setCallback(onMqttPublish);
  mqtt.subscribe(mqttCredentials.receiverTopic);
*/
  attachInterrupt(RAD, Counter, FALLING);

  Serial.println(F("******************************************************"));
  Serial.println(F("Loop starting..."));
  ticker.detach();
  wdt_reset();
  LED_L;
}

void loop() {
  iotConn.loop();
  /*
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
*/
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
