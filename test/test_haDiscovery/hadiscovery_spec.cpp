#include "haDiscovery.hpp"
#include "BDDTest.h"
#include "IPAddress.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// ---- CapturingClient ---------------------------------------------------
// Minimal TCP client that captures every byte PubSubClient writes and serves
// a pre-loaded CONNACK response so mqtt.connect() succeeds.

class CapturingClient : public Client {
public:
  static constexpr size_t CAPTURE_BUF = 4096U;
  static constexpr size_t RESP_BUF    = 16U;

  uint8_t capData[CAPTURE_BUF] = {};
  size_t  capLen = 0U;

private:
  uint8_t respData[RESP_BUF] = {};
  size_t  respLen = 0U;
  size_t  respPos = 0U;
  bool    _connected = false;

public:
  bool connect(IPAddress /*ip*/, uint16_t /*port*/) override {
    _connected = true; return true;
  }
  bool connect(const char* /*host*/, uint16_t /*port*/) override {
    _connected = true; return true;
  }
  size_t write(uint8_t b) override {
    if(capLen < CAPTURE_BUF) { capData[capLen++] = b; }
    return 1U;
  }
  size_t write(const uint8_t* buf, size_t size) override {
    for(size_t i = 0U; i < size; ++i) { write(buf[i]); }
    return size;
  }
  int16_t available() override {
    return static_cast<int16_t>(respLen - respPos);
  }
  int16_t read() override {
    return (respPos < respLen) ? static_cast<int16_t>(respData[respPos++]) : -1;
  }
  int16_t read(uint8_t* buf, size_t size) override {
    for(size_t i = 0U; i < size; ++i) {
      const int16_t c = read();
      if(c < 0) { return static_cast<int16_t>(i); }
      buf[i] = static_cast<uint8_t>(c);
    }
    return static_cast<int16_t>(size);
  }
  int16_t peek() override {
    return (respPos < respLen) ? static_cast<int16_t>(respData[respPos]) : -1;
  }
  void flush() override {}
  void stop() override { _connected = false; }
  uint8_t connected() override { return _connected ? 1U : 0U; }
  operator bool() override { return true; }

  void loadConnack() {
    respData[0] = 0x20U; respData[1] = 0x02U;
    respData[2] = 0x00U; respData[3] = 0x00U;
    respLen = 4U; respPos = 0U;
  }
  void resetCapture() { capLen = 0U; }
};

// ---- MQTT PUBLISH decoder -----------------------------------------------

struct PublishRecord {
  char topic[256]    = {};
  char payload[1024] = {};
  bool retained      = false;
  bool valid         = false;
};

static PublishRecord decodeMqttPublish(const uint8_t* buf, size_t len) {
  PublishRecord r;
  if(len < 4U) { return r; }
  const uint8_t fh = buf[0];
  if((fh >> 4U) != 3U) { return r; }  // not PUBLISH
  r.retained = (fh & 0x01U) != 0U;

  // Decode variable-length remaining field.
  size_t pos = 1U;
  uint32_t remaining = 0U, mult = 1U;
  uint8_t enc;
  do {
    if(pos >= len) { return r; }
    enc = buf[pos++];
    remaining += static_cast<uint32_t>(enc & 0x7FU) * mult;
    mult *= 128U;
  } while((enc & 0x80U) != 0U);

  if(pos + remaining > len) { return r; }

  // Topic.
  if(pos + 2U > len) { return r; }
  const uint16_t tlen = (static_cast<uint16_t>(buf[pos]) << 8U) | buf[pos + 1U];
  pos += 2U;
  if(tlen >= sizeof(r.topic) || pos + tlen > len) { return r; }
  memcpy(r.topic, buf + pos, tlen);
  r.topic[tlen] = '\0';
  pos += tlen;

  // Payload (QoS 0 — no packet identifier).
  const size_t plen = remaining - 2U - tlen;
  if(plen >= sizeof(r.payload)) { return r; }
  memcpy(r.payload, buf + pos, plen);
  r.payload[plen] = '\0';

  r.valid = true;
  return r;
}

// ---- Test fixture -------------------------------------------------------

static const uint8_t kServerIp[]     = { 127U, 0U, 0U, 1U };
static const char    kClientName[]   = "esp32_can_AABBCCDDEEFF";
static const char    kSenderTopic[]  = "iot/dtos/AABBCCDDEEFF/";
static const char    kRecvTopic[]    = "iot/stod/AABBCCDDEEFF/#";
static const char    kAvailTopic[]   = "iot/dtos/AABBCCDDEEFF/availability";

struct Fixture {
  CapturingClient cap;
  PubSubClient    mqtt;
  HADiscovery     had;

  Fixture() :
    mqtt(IPAddress(kServerIp), 1883U, cap),
    had(mqtt, kClientName, kSenderTopic, kRecvTopic, kAvailTopic)
  {
    cap.loadConnack();
    (void)mqtt.connect("test");
    cap.resetCapture();
  }

  PublishRecord capture() {
    return decodeMqttPublish(cap.capData, cap.capLen);
  }
};

// ---- Tests --------------------------------------------------------------

bool test_buildDeviceName_appears_in_payload() {
  IT("buildDeviceName formats prefix+mac correctly and it appears in the device block");
  Fixture f;
  const uint8_t mac[6] = { 0x00U, 0x11U, 0x22U, 0xAAU, 0xBBU, 0xCCU };
  f.had.buildDeviceName(mac, "mcu_smoke");

  const auto cfg = HADiscovery::EntityConfig::sensor(
    "Temperature", "{{ value_json.t }}", nullptr,
    HADiscovery::StateClass::none, HADiscovery::DeviceClass::none);
  IS_TRUE(f.had.publishEntity("temperature", cfg));

  const PublishRecord rec = f.capture();
  IS_TRUE(rec.valid);
  // Device name must be uppercase with spaces: "SMOKE AABBCC".
  IS_TRUE(strstr(rec.payload, "\"name\":\"SMOKE AABBCC\"") != nullptr);
  END_IT
}

bool test_publishEntity_sensor_discovery_topic() {
  IT("publishEntity builds the correct HA discovery topic for a sensor");
  Fixture f;
  const uint8_t mac[6] = {};
  f.had.buildDeviceName(mac, "mcu_smoke");

  const auto cfg = HADiscovery::EntityConfig::sensor("Temperature", "{{ value_json.t }}");
  IS_TRUE(f.had.publishEntity("temperature", cfg));

  const PublishRecord rec = f.capture();
  IS_TRUE(rec.valid);
  IS_EQUAL(strcmp(rec.topic,
    "homeassistant/sensor/esp32_can_AABBCCDDEEFF_temperature/config"), 0);
  IS_TRUE(rec.retained);
  END_IT
}

bool test_publishEntity_sensor_state_topic() {
  IT("publishEntity sets state_topic to senderTopic+subtopic");
  Fixture f;
  const uint8_t mac[6] = {};
  f.had.buildDeviceName(mac, "mcu_smoke");

  const auto cfg = HADiscovery::EntityConfig::sensor(
    "Temperature", "{{ value_json.t }}", nullptr,
    HADiscovery::StateClass::measurement, HADiscovery::DeviceClass::temperature);
  IS_TRUE(f.had.publishEntity("temperature", cfg));

  const PublishRecord rec = f.capture();
  IS_TRUE(rec.valid);
  IS_TRUE(strstr(rec.payload,
    "\"state_topic\":\"iot/dtos/AABBCCDDEEFF/temperature\"") != nullptr);
  IS_TRUE(strstr(rec.payload, "\"device_class\":\"temperature\"") != nullptr);
  IS_TRUE(strstr(rec.payload, "\"state_class\":\"measurement\"") != nullptr);
  END_IT
}

bool test_publishEntity_sensor_attributes_topic() {
  IT("publishEntity adds json_attributes_topic for state-topic entities");
  Fixture f;
  const uint8_t mac[6] = {};
  f.had.buildDeviceName(mac, "mcu_smoke");

  const auto cfg = HADiscovery::EntityConfig::sensor("S", "{{ value_json.v }}");
  IS_TRUE(f.had.publishEntity("data", cfg));

  const PublishRecord rec = f.capture();
  IS_TRUE(rec.valid);
  IS_TRUE(strstr(rec.payload,
    "\"json_attributes_topic\":\"iot/dtos/AABBCCDDEEFF/data\"") != nullptr);
  END_IT
}

bool test_publishEntity_button_command_topic() {
  IT("publishEntity uses command_topic and payload_press for button entities");
  Fixture f;
  const uint8_t mac[6] = {};
  f.had.buildDeviceName(mac, "mcu_smoke");

  const auto cfg = HADiscovery::EntityConfig::button(
    "Restart", "reboot", HADiscovery::DeviceClass::restart);
  IS_TRUE(f.had.publishEntity("restart", cfg));

  const PublishRecord rec = f.capture();
  IS_TRUE(rec.valid);
  IS_EQUAL(strcmp(rec.topic,
    "homeassistant/button/esp32_can_AABBCCDDEEFF_restart/config"), 0);
  IS_TRUE(strstr(rec.payload, "\"command_topic\":") != nullptr);
  IS_TRUE(strstr(rec.payload, "\"state_topic\":")    == nullptr);
  IS_TRUE(strstr(rec.payload, "\"json_attributes_topic\":")  == nullptr);
  IS_TRUE(strstr(rec.payload, "\"payload_press\":")            != nullptr);
  IS_TRUE(strstr(rec.payload, R"(\"cmd\":\"reboot\")")        != nullptr);
  END_IT
}

bool test_publishConnectivity_skips_availability_block() {
  IT("publishConnectivity omits availability and json_attributes_topic");
  Fixture f;
  const uint8_t mac[6] = {};
  f.had.buildDeviceName(mac, "mcu_smoke");

  IS_TRUE(f.had.publishConnectivity());

  const PublishRecord rec = f.capture();
  IS_TRUE(rec.valid);
  IS_EQUAL(strcmp(rec.topic,
    "homeassistant/binary_sensor/esp32_can_AABBCCDDEEFF_availability/config"), 0);
  IS_TRUE(strstr(rec.payload,
    "\"state_topic\":\"iot/dtos/AABBCCDDEEFF/availability\"") != nullptr);
  IS_TRUE(strstr(rec.payload, "\"availability\":[") == nullptr);
  IS_TRUE(strstr(rec.payload, "\"json_attributes_topic\":")  == nullptr);
  END_IT
}

bool test_publishCanDeviceEntity_discovery_topic() {
  IT("publishCanDeviceEntity builds discovery topic from canDevConfig.deviceId");
  Fixture f;

  const HADiscovery::EntityConfig cfg = HADiscovery::EntityConfig::sensor(
    "Temperature", "{{ value_json.t }}", nullptr,
    HADiscovery::StateClass::measurement, HADiscovery::DeviceClass::temperature);

  const HADiscovery::CanDeviceConfig canCfg = {
    "esp32_can_AABBCCDDEEFF_alert1",
    "ALERT1 DDEEFF",
    "771 (12345678)",
    "iot/dtos/AABBCCDDEEFF/alert1/availability",
    "alert1",
    "ATmega328P",
    false
  };

  IS_TRUE(f.had.publishCanDeviceEntity("temperature", cfg, canCfg));

  const PublishRecord rec = f.capture();
  IS_TRUE(rec.valid);
  IS_EQUAL(strcmp(rec.topic,
    "homeassistant/sensor/esp32_can_AABBCCDDEEFF_alert1_temperature/config"), 0);
  IS_TRUE(rec.retained);
  END_IT
}

bool test_publishCanDeviceEntity_state_topic_uses_dataSubtopic() {
  IT("publishCanDeviceEntity state_topic uses canDevConfig.dataSubtopic, not subtopic");
  Fixture f;

  const HADiscovery::EntityConfig cfg = HADiscovery::EntityConfig::sensor(
    "Temperature", "{{ value_json.t }}");

  const HADiscovery::CanDeviceConfig canCfg = {
    "esp32_can_AABBCCDDEEFF_alert1",
    "ALERT1 DDEEFF",
    "771 (12345678)",
    "iot/dtos/AABBCCDDEEFF/alert1/availability",
    "alert1",
    "ATmega328P",
    false
  };

  IS_TRUE(f.had.publishCanDeviceEntity("temperature", cfg, canCfg));

  const PublishRecord rec = f.capture();
  IS_TRUE(rec.valid);
  // state_topic must end with the data subtopic "alert1", not the entity subtopic "temperature".
  IS_TRUE(strstr(rec.payload,
    "\"state_topic\":\"iot/dtos/AABBCCDDEEFF/alert1\"") != nullptr);
  END_IT
}

bool test_publishCanDeviceEntity_dual_availability() {
  IT("publishCanDeviceEntity includes both availability topics and availability_mode");
  Fixture f;

  const HADiscovery::EntityConfig cfg = HADiscovery::EntityConfig::sensor(
    "Temperature", "{{ value_json.t }}");

  const HADiscovery::CanDeviceConfig canCfg = {
    "esp32_can_AABBCCDDEEFF_alert1",
    "ALERT1 DDEEFF",
    "771 (12345678)",
    "iot/dtos/AABBCCDDEEFF/alert1/availability",
    "alert1",
    "ATmega328P",
    false
  };

  IS_TRUE(f.had.publishCanDeviceEntity("temperature", cfg, canCfg));

  const PublishRecord rec = f.capture();
  IS_TRUE(rec.valid);
  IS_TRUE(strstr(rec.payload, "iot/dtos/AABBCCDDEEFF/availability")       != nullptr);
  IS_TRUE(strstr(rec.payload, "iot/dtos/AABBCCDDEEFF/alert1/availability") != nullptr);
  IS_TRUE(strstr(rec.payload, "\"availability_mode\":\"all\"")             != nullptr);
  END_IT
}

bool test_publishCanDeviceEntity_skip_can_avail() {
  IT("publishCanDeviceEntity with skipCanAvailability omits the second avail entry");
  Fixture f;

  const HADiscovery::EntityConfig cfg = HADiscovery::EntityConfig::binarySensor(
    "Connection", "{{ value_json.state }}", "online", "offline",
    HADiscovery::DeviceClass::connectivity);

  const HADiscovery::CanDeviceConfig canCfg = {
    "esp32_can_AABBCCDDEEFF_alert1",
    "ALERT1 DDEEFF",
    "771 (12345678)",
    "iot/dtos/AABBCCDDEEFF/alert1/availability",
    "alert1/availability",
    "ATmega328P",
    true   // skipCanAvailability
  };

  IS_TRUE(f.had.publishCanDeviceEntity("connectivity", cfg, canCfg));

  const PublishRecord rec = f.capture();
  IS_TRUE(rec.valid);
  IS_TRUE(strstr(rec.payload, "iot/dtos/AABBCCDDEEFF/availability") != nullptr);
  IS_TRUE(strstr(rec.payload, "\"availability_mode\"")              == nullptr);
  END_IT
}

bool test_publishEntity_returns_false_when_disconnected() {
  IT("publishEntity returns false when the MQTT client is not connected");
  CapturingClient cap;
  PubSubClient mqtt(IPAddress(kServerIp), 1883U, cap);
  HADiscovery had(mqtt, kClientName, kSenderTopic, kRecvTopic, kAvailTopic);
  // cap never connects → mqtt is in DISCONNECTED state

  const auto cfg = HADiscovery::EntityConfig::sensor("S", "{{ value_json.v }}");
  IS_FALSE(had.publishEntity("data", cfg));
  END_IT
}

int main() {
  SUITE("HADiscovery");
  test_buildDeviceName_appears_in_payload();
  test_publishEntity_sensor_discovery_topic();
  test_publishEntity_sensor_state_topic();
  test_publishEntity_sensor_attributes_topic();
  test_publishEntity_button_command_topic();
  test_publishConnectivity_skips_availability_block();
  test_publishCanDeviceEntity_discovery_topic();
  test_publishCanDeviceEntity_state_topic_uses_dataSubtopic();
  test_publishCanDeviceEntity_dual_availability();
  test_publishCanDeviceEntity_skip_can_avail();
  test_publishEntity_returns_false_when_disconnected();
  FINISH
}
