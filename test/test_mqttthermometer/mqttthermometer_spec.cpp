#include "mqttThermometer.hpp"
#include "Arduino.h"
#include "BDDTest.h"
#include <string>

static constexpr uint8_t MAX_SENSORS = 4U;
static constexpr uint8_t ONE_WIRE_PIN = 5U;
static constexpr uint32_t MEASURE_PERIOD_MS = 60000U;

using Reader = Ds18b20Reader<MAX_SENSORS>;

static void resetEnv() {
  MqttBase::resetState();
  Reader::resetState();
  setFakeMillis(0U);
}

// Walks one full measurement cycle: request -> conversion delay -> one read per run().
static void runCycle(Task& task, uint32_t& now, uint8_t sensorCount) {
  setFakeMillis(now);
  (void)task.run();                                  // Idle: requestConversion()
  now += 751U;                                       // > 750 ms conversion delay
  setFakeMillis(now);
  (void)task.run();                                  // Converting -> Reading
  for(uint8_t i = 0U; i < sensorCount; ++i) {
    (void)task.run();                                // one sensor per run
  }
}

bool test_publishes_one_message_per_sensor() {
  IT("a measurement cycle publishes one JSON per sensor on temp/<rom>");
  resetEnv();
  Reader::fakeSensorCount = 2U;
  Reader::fakeTempsC[0] = 23.5F;
  Reader::fakeTempsC[1] = -10.25F;
  Connectivity conn;
  MqttThermometer<MAX_SENSORS> thermometer(conn, "temp", ONE_WIRE_PIN, MEASURE_PERIOD_MS);
  Task& task = thermometer;
  IS_TRUE(task.init());
  uint32_t now = 0U;
  runCycle(task, now, 2U);
  IS_EQUAL(Reader::requestCount, 1);
  IS_EQUAL(MqttBase::subtopicMessages.size(), 2U);
  IS_TRUE(MqttBase::subtopicMessages[0].first  == "temp/28ff000000000000");
  IS_TRUE(MqttBase::subtopicMessages[0].second == R"({"tempC":23.50})");
  IS_TRUE(MqttBase::subtopicMessages[1].first  == "temp/28ff000000000001");
  IS_TRUE(MqttBase::subtopicMessages[1].second == R"({"tempC":-10.25})");
  END_IT
}

bool test_disconnected_sensor_is_skipped() {
  IT("a sensor reading below -55 C is treated as disconnected and not published");
  resetEnv();
  Reader::fakeSensorCount = 2U;
  Reader::fakeTempsC[0] = Reader::invalidTempC;      // disconnected
  Reader::fakeTempsC[1] = 21.0F;
  Connectivity conn;
  MqttThermometer<MAX_SENSORS> thermometer(conn, "temp", ONE_WIRE_PIN, MEASURE_PERIOD_MS);
  Task& task = thermometer;
  IS_TRUE(task.init());
  uint32_t now = 0U;
  runCycle(task, now, 2U);
  IS_EQUAL(MqttBase::subtopicMessages.size(), 1U);
  IS_TRUE(MqttBase::subtopicMessages[0].first == "temp/28ff000000000001");
  END_IT
}

bool test_waits_measure_period_between_cycles() {
  IT("a new conversion starts only after the measure period elapses");
  resetEnv();
  Reader::fakeSensorCount = 1U;
  Reader::fakeTempsC[0] = 20.0F;
  Connectivity conn;
  MqttThermometer<MAX_SENSORS> thermometer(conn, "temp", ONE_WIRE_PIN, MEASURE_PERIOD_MS);
  Task& task = thermometer;
  IS_TRUE(task.init());
  uint32_t now = 0U;
  runCycle(task, now, 1U);                           // Waiting state from here
  (void)task.run();
  IS_EQUAL(Reader::requestCount, 1);                 // still waiting
  now += MEASURE_PERIOD_MS + 1U;
  setFakeMillis(now);
  (void)task.run();                                  // Waiting -> Idle
  (void)task.run();                                  // Idle: next conversion requested
  IS_EQUAL(Reader::requestCount, 2);
  END_IT
}

bool test_zero_sensors_is_safe() {
  IT("an empty 1-Wire bus initialises fine and run() stays idle");
  resetEnv();
  Reader::fakeSensorCount = 0U;
  Connectivity conn;
  MqttThermometer<MAX_SENSORS> thermometer(conn, "temp", ONE_WIRE_PIN, MEASURE_PERIOD_MS);
  Task& task = thermometer;
  IS_TRUE(task.init());                              // empty bus does not block boot
  (void)task.run();
  (void)task.run();
  IS_EQUAL(Reader::requestCount, 0);
  IS_EQUAL(MqttBase::subtopicMessages.size(), 0U);
  END_IT
}

bool test_discovery_publishes_each_sensor_as_device() {
  IT("publishDiscovery() publishes one HA device per sensor");
  resetEnv();
  Reader::fakeSensorCount = 3U;
  Connectivity conn;
  MqttThermometer<MAX_SENSORS> thermometer(conn, "temp", ONE_WIRE_PIN, MEASURE_PERIOD_MS);
  Task& task = thermometer;
  IS_TRUE(task.init());
  MqttBase& mqttSide = thermometer;
  IS_TRUE(mqttSide.publishDiscovery());
  IS_EQUAL(MqttBase::canDiscoverySubtopics.size(), 3U);
  IS_TRUE(MqttBase::canDiscoverySubtopics[0] == "temperature");
  END_IT
}

int main() {
  SUITE("MqttThermometer");
  test_publishes_one_message_per_sensor();
  test_disconnected_sensor_is_skipped();
  test_waits_measure_period_between_cycles();
  test_zero_sensors_is_safe();
  test_discovery_publishes_each_sensor_as_device();
  FINISH
}
