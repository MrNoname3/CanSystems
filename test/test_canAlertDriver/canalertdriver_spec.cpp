#include "canAlertDriver.hpp"
#include "Arduino.h"
#include "LittleFS.h"
#include "BDDTest.h"
#include <string>

// CanAlertDriver's constructor registers the instance in the global OtaRegistry, which has no
// remove operation — a destroyed driver would leave a dangling pointer behind for later tests.
// Every driver below is therefore a function-local static: constructed once, never destroyed.

static void resetEnv() {
  LittleFS.reset();
  MqttBase::resetState();
  CanHandler::resetState();
  setFakeMillis(0U);
}

static void injectMessage(CanAlertDriver& driver, const char* json) {
  MqttBase& mqttSide = driver;
  JsonDocument doc;
  (void)deserializeJson(doc, json);
  mqttSide.messageArrivedCallback(doc);
}

static void injectFrame(CanAlertDriver& driver, CanCmd cmd, const uint8_t (&data)[8]) {
  CanBase& canSide = driver;
  canSide.canFrameArrivedCallback(CanHandler::CanFrame(10U, static_cast<uint16_t>(cmd), 26U, data));
}

static const CanHandler::CanFrame* lastFrame(CanCmd cmd) {
  for(auto it = CanHandler::sentFrames.rbegin(); it != CanHandler::sentFrames.rend(); ++it) {
    if(static_cast<uint16_t>(it->cmd) == static_cast<uint16_t>(cmd)) { return &(*it); }
  }
  return nullptr;
}

// ---- MQTT message -> CAN frame mapping ----

bool test_colors_only_sends_rgb_led() {
  IT("a Colors-only message sends an RGB_LED frame with the color bytes");
  resetEnv();
  static CanHandler can;
  static Connectivity conn;
  static CanAlertDriver driver(can, 26U, conn, "alert1");
  injectMessage(driver, R"({"Colors":[3,2,1]})");
  const CanHandler::CanFrame* frame = lastFrame(CanCmd::RGB_LED);
  IS_TRUE(frame != nullptr);
  IS_EQUAL(frame->data[0], 3U);
  IS_EQUAL(frame->data[1], 2U);
  IS_EQUAL(frame->data[2], 1U);
  END_IT
}

bool test_sound_volume_colors_sends_play_mp3() {
  IT("a Sound+Volume+Colors message sends a PLAY_MP3 frame with sound, volume and colors");
  resetEnv();
  static CanHandler can;
  static Connectivity conn;
  static CanAlertDriver driver(can, 27U, conn, "alert2");
  injectMessage(driver, R"({"Sound":300,"Volume":20,"Colors":[7,8,9]})");
  const CanHandler::CanFrame* frame = lastFrame(CanCmd::PLAY_MP3);
  IS_TRUE(frame != nullptr);
  IS_EQUAL(frame->data[0], static_cast<uint8_t>(300U & 0xFFU));
  IS_EQUAL(frame->data[1], static_cast<uint8_t>(300U >> 8U));
  IS_EQUAL(frame->data[2], 20U);
  IS_EQUAL(frame->data[3], 7U);
  IS_EQUAL(frame->data[4], 8U);
  IS_EQUAL(frame->data[5], 9U);
  END_IT
}

bool test_sound_without_colors_plays_with_dark_leds() {
  IT("a Sound+Volume message without Colors sends PLAY_MP3 with zeroed color bytes");
  resetEnv();
  static CanHandler can;
  static Connectivity conn;
  static CanAlertDriver driver(can, 28U, conn, "alert3");
  injectMessage(driver, R"({"Sound":5,"Volume":15})");
  const CanHandler::CanFrame* frame = lastFrame(CanCmd::PLAY_MP3);
  IS_TRUE(frame != nullptr);
  IS_EQUAL(frame->data[0], 5U);
  IS_EQUAL(frame->data[1], 0U);
  IS_EQUAL(frame->data[2], 15U);
  IS_EQUAL(frame->data[3], 0U);                       // LEDs stay dark
  IS_EQUAL(frame->data[4], 0U);
  IS_EQUAL(frame->data[5], 0U);
  END_IT
}

bool test_malformed_colors_drops_message() {
  IT("a present but malformed Colors array drops the whole message");
  resetEnv();
  static CanHandler can;
  static Connectivity conn;
  static CanAlertDriver driver(can, 29U, conn, "alert4");
  injectMessage(driver, R"({"Sound":5,"Volume":15,"Colors":[1,2]})");        // wrong size
  injectMessage(driver, R"({"Sound":5,"Volume":15,"Colors":[1,2,300]})");    // out of uint8_t range
  injectMessage(driver, R"({"Sound":5,"Volume":15,"Colors":"red"})");        // wrong type
  injectMessage(driver, R"({"Colors":[1,"x",3]})");                          // bad element, no sound
  IS_EQUAL(CanHandler::sentFrames.size(), 0U);
  END_IT
}

// ---- CAN frame -> MQTT message mapping ----

bool test_hum_temp_ldr_frame_publishes_json() {
  IT("a READ_HUM_TEMP_LDR frame publishes temperature (with offset), humidity and light");
  resetEnv();
  static CanHandler can;
  static Connectivity conn;
  static CanAlertDriver driver(can, 30U, conn, "alert5", -0.5F);
  // temp raw = 2345 (23.45 C), humidity = 55, light = 1023.
  const uint8_t data[8] = { 0x29U, 0x09U, 55U, 0U, 0xFFU, 0x03U, 0U, 0U };
  injectFrame(driver, CanCmd::READ_HUM_TEMP_LDR, data);
  IS_EQUAL(MqttBase::messageCount, 1);
  IS_TRUE(MqttBase::lastMessage == R"({"Temperature":22.95,"Humidity":55,"Light":1023})");
  END_IT
}

bool test_unknown_frame_is_ignored() {
  IT("an unrelated CAN command produces no MQTT traffic");
  resetEnv();
  static CanHandler can;
  static Connectivity conn;
  static CanAlertDriver driver(can, 31U, conn, "alert6");
  const uint8_t data[8] = { 0U };
  injectFrame(driver, CanCmd::LOOP_TIME_MAX, data);
  IS_EQUAL(MqttBase::messageCount, 0);
  IS_EQUAL(MqttBase::subtopicMessages.size(), 0U);
  END_IT
}

// ---- HA discovery ----

bool test_publish_discovery_covers_all_entities() {
  IT("publishDiscovery() publishes the three sensors and the connectivity entity");
  resetEnv();
  static CanHandler can;
  static Connectivity conn;
  static CanAlertDriver driver(can, 32U, conn, "alert7");
  MqttBase& mqttSide = driver;
  IS_TRUE(mqttSide.publishDiscovery());
  IS_EQUAL(MqttBase::canDiscoverySubtopics.size(), 4U);
  IS_TRUE(MqttBase::canDiscoverySubtopics[0] == "temperature");
  IS_TRUE(MqttBase::canDiscoverySubtopics[1] == "humidity");
  IS_TRUE(MqttBase::canDiscoverySubtopics[2] == "illuminance");
  IS_TRUE(MqttBase::canDiscoverySubtopics[3] == "connectivity");
  END_IT
}

int main() {
  SUITE("CanAlertDriver");
  test_colors_only_sends_rgb_led();
  test_sound_volume_colors_sends_play_mp3();
  test_sound_without_colors_plays_with_dark_leds();
  test_malformed_colors_drops_message();
  test_hum_temp_ldr_frame_publishes_json();
  test_unknown_frame_is_ignored();
  test_publish_discovery_covers_all_entities();
  FINISH
}
