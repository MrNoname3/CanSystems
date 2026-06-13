#include "radiation.hpp"
#include "Arduino.h"
#include "LittleFS.h"
#include "Ticker.h"
#include "BDDTest.h"
#include <string>

static constexpr uint8_t RAD_PIN = 2U;
static constexpr const char* kTubeConfig = "/config/tube.json";

static void resetEnv() {
  LittleFS.reset();
  MqttBase::resetState();
  Ticker::resetState();
  resetGpioState();
  setFakeMillis(0U);
}

// Simulates N Geiger pulses through the pin interrupt stored by the Arduino shim.
static void pulse(uint32_t count) {
  for(uint32_t i = 0U; i < count; ++i) { triggerInterrupt(RAD_PIN); }
}

// Closes a measurement window (the 1-minute Ticker fires) and lets run() publish it.
static void measureAndRun(Task& task) {
  Ticker::fireLast();
  (void)task.run();
}

bool test_m4011_tube_payload() {
  IT("an M4011 tube converts CPM to sievert/radian with 4/2-digit fixed point");
  resetEnv();
  LittleFS.setFile(kTubeConfig, R"({"tube":2})");
  Connectivity conn;
  Radiation radiation(conn, "radiation", RAD_PIN);
  Task& task = radiation;
  IS_TRUE(task.init());
  pulse(154U);                                       // 154 / 153.8 = 1.0013 uSv/h
  measureAndRun(task);
  IS_EQUAL(MqttBase::messageCount, 1);
  IS_TRUE(MqttBase::lastMessage == R"({"tick":154,"sievert":1.0013,"radian":100.13})");
  END_IT
}

bool test_j305_tube_payload_rounds_correctly() {
  IT("a J305 tube payload rounds the fixed-point value instead of truncating");
  resetEnv();
  LittleFS.setFile(kTubeConfig, R"({"tube":1})");
  Connectivity conn;
  Radiation radiation(conn, "radiation", RAD_PIN);
  Task& task = radiation;
  IS_TRUE(task.init());
  pulse(123U);                                       // 123 / 123.153 * 10000 = 9987.58 -> 9988
  measureAndRun(task);
  IS_TRUE(MqttBase::lastMessage == R"({"tick":123,"sievert":0.9988,"radian":99.88})");
  END_IT
}

bool test_unknown_tube_reports_zero_dose() {
  IT("a missing tube config still reports ticks but zero sievert/radian");
  resetEnv();                                        // no /config/tube.json
  Connectivity conn;
  Radiation radiation(conn, "radiation", RAD_PIN);
  Task& task = radiation;
  IS_TRUE(task.init());
  pulse(42U);
  measureAndRun(task);
  IS_TRUE(MqttBase::lastMessage == R"({"tick":42,"sievert":0.0000,"radian":0.00})");
  END_IT
}

bool test_counter_resets_between_windows() {
  IT("each measurement window counts only its own pulses");
  resetEnv();
  LittleFS.setFile(kTubeConfig, R"({"tube":2})");
  Connectivity conn;
  Radiation radiation(conn, "radiation", RAD_PIN);
  Task& task = radiation;
  IS_TRUE(task.init());
  pulse(100U);
  measureAndRun(task);
  pulse(5U);                                         // second window: only 5 new pulses
  measureAndRun(task);
  IS_EQUAL(MqttBase::messageCount, 2);
  IS_TRUE(MqttBase::lastMessage.find(R"("tick":5,)") != std::string::npos);
  END_IT
}

bool test_no_publish_without_completed_measurement() {
  IT("run() publishes nothing while a measurement window is still open");
  resetEnv();
  LittleFS.setFile(kTubeConfig, R"({"tube":2})");
  Connectivity conn;
  Radiation radiation(conn, "radiation", RAD_PIN);
  Task& task = radiation;
  IS_TRUE(task.init());
  pulse(10U);
  (void)task.run();                                  // ticker has not fired yet
  IS_EQUAL(MqttBase::messageCount, 0);
  END_IT
}

bool test_end_detaches_counting_and_ticker() {
  IT("end() detaches the pulse interrupt and the measurement ticker");
  resetEnv();
  LittleFS.setFile(kTubeConfig, R"({"tube":2})");
  Connectivity conn;
  Radiation radiation(conn, "radiation", RAD_PIN);
  Task& task = radiation;
  IS_TRUE(task.init());
  radiation.end();
  pulse(50U);                                        // no handler attached anymore
  measureAndRun(task);                               // no ticker attached anymore
  IS_EQUAL(MqttBase::messageCount, 0);
  END_IT
}

int main() {
  SUITE("Radiation");
  test_m4011_tube_payload();
  test_j305_tube_payload_rounds_correctly();
  test_unknown_tube_reports_zero_dose();
  test_counter_resets_between_windows();
  test_no_publish_without_completed_measurement();
  test_end_detaches_counting_and_ticker();
  FINISH
}
