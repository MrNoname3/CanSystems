#include "rfHandler.hpp"
#include "Arduino.h"
#include "RCSwitch.h"
#include "BDDTest.h"
#include <string>

static constexpr uint8_t RX_PIN = 1U;
static constexpr uint8_t TX_PIN = 3U;

static void resetEnv() {
  MqttBase::resetState();
  RCSwitch::resetState();
  resetGpioState();
  setFakeMillis(0U);
}

// ---- receive path (run) ----

bool test_received_frame_is_published_as_json() {
  IT("run() publishes a received RF frame as the RfReceived JSON");
  resetEnv();
  Connectivity conn;
  RfHandler rf(conn, "rf433", RX_PIN, TX_PIN);
  Task& task = rf;
  RCSwitch::injectReceived(123456ULL, 24U, 1U, 350U);
  IS_TRUE(task.run());
  IS_EQUAL(MqttBase::messageCount, 1);
  IS_TRUE(MqttBase::lastMessage ==
          R"({"RfReceived":{"Data":123456,"Bits":24,"Protocol":1,"Pulse":350}})");
  IS_FALSE(RCSwitch::rxAvailable);                   // resetAvailable() was called
  END_IT
}

bool test_no_frame_publishes_nothing() {
  IT("run() with no frame available publishes nothing");
  resetEnv();
  Connectivity conn;
  RfHandler rf(conn, "rf433", RX_PIN, TX_PIN);
  Task& task = rf;
  IS_TRUE(task.run());
  IS_EQUAL(MqttBase::messageCount, 0);
  END_IT
}

bool test_duplicate_within_window_is_filtered() {
  IT("an identical frame arriving within the dedup window is suppressed");
  resetEnv();
  Connectivity conn;
  RfHandler rf(conn, "rf433", RX_PIN, TX_PIN);
  Task& task = rf;
  RCSwitch::injectReceived(555ULL, 24U, 1U, 320U);
  IS_TRUE(task.run());
  IS_EQUAL(MqttBase::messageCount, 1);
  setFakeMillis(50U);                                // < 100 ms dedup window
  RCSwitch::injectReceived(555ULL, 24U, 1U, 320U);   // same data/bits/protocol
  IS_TRUE(task.run());
  IS_EQUAL(MqttBase::messageCount, 1);               // still 1: duplicate filtered
  END_IT
}

bool test_duplicate_after_window_passes() {
  IT("the same frame is published again once the dedup window has elapsed");
  resetEnv();
  Connectivity conn;
  RfHandler rf(conn, "rf433", RX_PIN, TX_PIN);
  Task& task = rf;
  RCSwitch::injectReceived(555ULL, 24U, 1U, 320U);
  IS_TRUE(task.run());
  setFakeMillis(101U);                               // > 100 ms: window expired, last data cleared
  RCSwitch::injectReceived(555ULL, 24U, 1U, 320U);
  IS_TRUE(task.run());
  IS_EQUAL(MqttBase::messageCount, 2);
  END_IT
}

bool test_different_frame_within_window_passes() {
  IT("a frame differing in data is published even within the dedup window");
  resetEnv();
  Connectivity conn;
  RfHandler rf(conn, "rf433", RX_PIN, TX_PIN);
  Task& task = rf;
  RCSwitch::injectReceived(555ULL, 24U, 1U, 320U);
  IS_TRUE(task.run());
  setFakeMillis(20U);
  RCSwitch::injectReceived(777ULL, 24U, 1U, 320U);   // different data
  IS_TRUE(task.run());
  IS_EQUAL(MqttBase::messageCount, 2);
  END_IT
}

// ---- transmit path (messageArrivedCallback) ----

bool test_mqtt_message_transmits_rf() {
  IT("a complete MQTT message sets protocol, pulse length and transmits");
  resetEnv();
  Connectivity conn;
  RfHandler rf(conn, "rf433", RX_PIN, TX_PIN);
  MqttBase& mqttSide = rf;
  JsonDocument doc;
  IS_TRUE(deserializeJson(doc,
    R"({"Data":4096,"Bits":24,"Protocol":2,"Pulse":400})") == DeserializationError::Ok);
  mqttSide.messageArrivedCallback(doc);
  IS_EQUAL(RCSwitch::lastProtocol, 2);
  IS_EQUAL(RCSwitch::lastPulseLength, 400);
  IS_EQUAL(RCSwitch::sendCount, 1);
  IS_EQUAL(RCSwitch::lastSentCode, 4096ULL);
  IS_EQUAL(RCSwitch::lastSentLength, 24U);
  END_IT
}

bool test_mqtt_message_without_data_does_not_transmit() {
  IT("an MQTT message with zero data sets protocol/pulse but does not transmit");
  resetEnv();
  Connectivity conn;
  RfHandler rf(conn, "rf433", RX_PIN, TX_PIN);
  MqttBase& mqttSide = rf;
  JsonDocument doc;
  IS_TRUE(deserializeJson(doc,
    R"({"Data":0,"Bits":24,"Protocol":2,"Pulse":400})") == DeserializationError::Ok);
  mqttSide.messageArrivedCallback(doc);
  IS_EQUAL(RCSwitch::lastProtocol, 2);
  IS_EQUAL(RCSwitch::lastPulseLength, 400);
  IS_EQUAL(RCSwitch::sendCount, 0);                  // data == 0 -> no send
  END_IT
}

bool test_mqtt_message_missing_fields_is_ignored() {
  IT("an MQTT message missing required fields transmits nothing");
  resetEnv();
  Connectivity conn;
  RfHandler rf(conn, "rf433", RX_PIN, TX_PIN);
  MqttBase& mqttSide = rf;
  JsonDocument doc;
  IS_TRUE(deserializeJson(doc, R"({"Data":4096})") == DeserializationError::Ok);
  mqttSide.messageArrivedCallback(doc);
  IS_EQUAL(RCSwitch::sendCount, 0);
  IS_EQUAL(RCSwitch::lastProtocol, 0);
  END_IT
}

int main() {
  SUITE("RfHandler");
  test_received_frame_is_published_as_json();
  test_no_frame_publishes_nothing();
  test_duplicate_within_window_is_filtered();
  test_duplicate_after_window_passes();
  test_different_frame_within_window_passes();
  test_mqtt_message_transmits_rf();
  test_mqtt_message_without_data_does_not_transmit();
  test_mqtt_message_missing_fields_is_ignored();
  FINISH
}
