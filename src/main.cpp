#include "main.hpp"

//--- Variables ---//
const uint8_t measure_time = 60;  //sec
uint32_t cpm = 0;

//--- Debug LED ---//
Ticker ticker;

class Radiation : public MqttComBase {

public:

  //Radiation() = default;
  Radiation(const char* classID) : MqttComBase(classID) {}

  /// @brief Destructor of the object.
  virtual ~Radiation() = default;

  virtual void messageReceived(uint8_t* payload, uint32_t length) const override {
    Serial.printf("Class: %s\r\n", MqttComBase::getClassId());
  }

  virtual void messageSend() const override {
    const uint8_t* id = reinterpret_cast<const uint8_t*>(MqttComBase::getClassId());
    MqttComBase::sendToMqtt(id, 16);
  }

private:

};

Radiation radiation("radiation1");
Radiation radiation2("radiation2");
const MqttComBase* Connectivity::messageMap[] = { &radiation, &radiation2, nullptr };

//--- Networking ---//
const char Connectivity::DEVICE_TOPIC[] PROGMEM = "test";
Connectivity iotConn(&Serial, SPI_CS);

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

  attachInterrupt(RAD, Counter, FALLING);

  Serial.println(F("******************************************************"));
  Serial.println(F("Loop starting..."));
  ticker.detach();
  wdt_reset();
  LED_L;
}

void loop() {
  static uint32_t testTimer = millis();
  if(millis() - testTimer >= 5000) {
    testTimer = millis();
    radiation.messageSend();
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

*/

  yield();
  iotConn.loop();
  wdt_reset();
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
