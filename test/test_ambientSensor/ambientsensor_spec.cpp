#include "ambientSensor.hpp"
#include "Arduino.h"
#include "Wire.h"
#include "EEPROM.h"
#include "BDDTest.h"
#include <string.h>

// ---- TestCanHandler mock ----

class TestCanHandler final : public CanHandlerBase {
public:
  using CanHandlerBase::send;
  mutable uint32_t sendCount   = 0U;
  mutable uint16_t lastCommand = 0U;
  mutable uint8_t  lastData[8] = {};

  bool init() override { return true; } // NOLINT(readability-make-member-function-const)
  bool run()  override { return true; } // NOLINT(readability-make-member-function-const)

  bool send(uint16_t command, const uint8_t (&data)[8]) const override {
    sendCount++;
    lastCommand = command;
    memcpy(lastData, data, 8U);
    return true;
  }
};

// ---- Wire helpers ----
// Preload the 2 bytes consumed by setHeater() and setPrecision() USER1_READ calls
// then 4 bytes for one temperature + humidity measurement.
//
// SI7021 raw encoding (from si7021.cpp):
//   temperature = ((17572 * raw) >> 16) - 4685   [hundredths of °C]
//   humidity    = ((125   * raw) >> 16) - 6       [%]
//
// Precomputed values used in tests:
//   temp 25.00°C → raw=0x68AD  bytes={0x68,0xAD}
//   temp 25.51°C → raw=0x696C  bytes={0x69,0x6C}   (25.00+0.51, exceeds kTempTolerance=50)
//   hum  50%     → raw=0x72B1  bytes={0x72,0xB1}
//   hum  54%     → raw=0x7AE2  bytes={0x7A,0xE2}   (50+4,       exceeds kHumTolerance=3)

static void preloadInit() {
  Wire.reset();
  Wire.setEndTransmissionResult(0U);
  Wire.addReadByte(0x00U); // setHeater USER1_READ byte
  Wire.addReadByte(0x00U); // setPrecision USER1_READ byte
}

static void preloadMeasurement(uint8_t tMsb, uint8_t tLsb, uint8_t hMsb, uint8_t hLsb) {
  Wire.addReadByte(tMsb);
  Wire.addReadByte(tLsb);
  Wire.addReadByte(hMsb);
  Wire.addReadByte(hLsb);
}

// Advance the sensor through one full measurement cycle starting at 'startMs'.
// measurePeriod must have elapsed relative to eventTimer.
// Calls run() the exact number of times to reach IDLE again.
static void runCycle(AmbientSensor& s, uint32_t msWhenIdle) {
  setFakeMillis(msWhenIdle);
  s.run(); // IDLE   → READ_TEMPERATURE  (timer fires)
  s.run(); // READ_T → READ_HUMIDITY
  s.run(); // READ_H → CHECK_SEND
  s.run(); // CHECK  → SEND_VALUES or IDLE
  s.run(); // SEND   → IDLE  (no-op if already IDLE)
}

static constexpr uint32_t kMeasPeriod = 100U; // short period for tests
static constexpr uint8_t  kLightPin   = 3U;

// ---- init ----

bool test_init_fails_when_si7021_not_found() {
  IT("init() returns false when SI7021 does not ACK on I2C");
  Wire.reset();
  Wire.setEndTransmissionResult(1U); // NACK → device not found
  TestCanHandler ch;
  AmbientSensor s(ch, kLightPin, kMeasPeriod);
  setFakeMillis(0U);
  IS_FALSE(s.init());
  clearFakeMillis();
  END_IT
}

bool test_init_succeeds_when_si7021_present() {
  IT("init() returns true when SI7021 ACKs on I2C");
  preloadInit();
  TestCanHandler ch;
  AmbientSensor s(ch, kLightPin, kMeasPeriod);
  setFakeMillis(0U);
  IS_TRUE(s.init());
  clearFakeMillis();
  END_IT
}

// ---- no send before measurePeriod ----

bool test_no_send_before_measure_period() {
  IT("no CAN frame is sent while measurePeriod has not elapsed");
  preloadInit();
  TestCanHandler ch;
  AmbientSensor s(ch, kLightPin, kMeasPeriod);
  setFakeMillis(0U);
  s.init();
  setFakeMillis(kMeasPeriod - 1U); // just short of the period
  s.run(); // stays IDLE
  IS_EQUAL(ch.sendCount, 0U);
  clearFakeMillis();
  END_IT
}

// ---- first cycle always sends (sentinel values guarantee threshold) ----

bool test_first_cycle_sends_values() {
  IT("first measurement cycle always sends because lastSent* are sentinels");
  preloadInit();
  preloadMeasurement(0x68U, 0xADU, 0x72U, 0xB1U); // 25°C, 50%
  setAnalogReadValue(0U);
  TestCanHandler ch;
  AmbientSensor s(ch, kLightPin, kMeasPeriod);
  setFakeMillis(0U);
  s.init();
  runCycle(s, kMeasPeriod + 1U);
  IS_EQUAL(ch.sendCount, 1U);
  IS_EQUAL(ch.lastCommand, static_cast<uint16_t>(CanCmd::READ_HUM_TEMP_LDR));
  // Verify temperature bytes in payload: 2500 = 0x09C4
  IS_EQUAL(ch.lastData[0], static_cast<uint8_t>(0xC4U)); // low byte
  IS_EQUAL(ch.lastData[1], static_cast<uint8_t>(0x09U)); // high byte
  // Humidity 50 = 0x0032
  IS_EQUAL(ch.lastData[2], static_cast<uint8_t>(50U));
  IS_EQUAL(ch.lastData[3], static_cast<uint8_t>(0U));
  clearFakeMillis();
  END_IT
}

// ---- same values → no send ----

bool test_second_cycle_same_values_no_send() {
  IT("identical readings in the second cycle do not trigger a CAN send");
  preloadInit();
  preloadMeasurement(0x68U, 0xADU, 0x72U, 0xB1U); // first cycle: 25°C 50%
  setAnalogReadValue(0U);
  TestCanHandler ch;
  AmbientSensor s(ch, kLightPin, kMeasPeriod);
  setFakeMillis(0U);
  s.init();
  runCycle(s, kMeasPeriod + 1U);       // first cycle → sends

  preloadMeasurement(0x68U, 0xADU, 0x72U, 0xB1U); // second cycle: same values
  ch.sendCount = 0U;
  runCycle(s, 2U * (kMeasPeriod + 1U));
  IS_EQUAL(ch.sendCount, 0U);
  clearFakeMillis();
  END_IT
}

// ---- temperature exceeds tolerance → sends ----

bool test_temp_change_above_tolerance_triggers_send() {
  IT("temperature change > kTempTolerance (0.50°C) triggers an immediate send");
  preloadInit();
  preloadMeasurement(0x68U, 0xADU, 0x72U, 0xB1U); // first: 25.00°C
  setAnalogReadValue(0U);
  TestCanHandler ch;
  AmbientSensor s(ch, kLightPin, kMeasPeriod);
  setFakeMillis(0U);
  s.init();
  runCycle(s, kMeasPeriod + 1U);

  preloadMeasurement(0x69U, 0x6CU, 0x72U, 0xB1U); // second: 25.51°C  (+51 > 50)
  ch.sendCount = 0U;
  runCycle(s, 2U * (kMeasPeriod + 1U));
  IS_EQUAL(ch.sendCount, 1U);
  clearFakeMillis();
  END_IT
}

// ---- temperature at tolerance boundary → no send ----

bool test_temp_change_at_tolerance_no_send() {
  IT("temperature change == kTempTolerance (0.50°C) does not trigger a send (strictly greater required)");
  preloadInit();
  preloadMeasurement(0x68U, 0xADU, 0x72U, 0xB1U); // first: 25.00°C
  setAnalogReadValue(0U);
  TestCanHandler ch;
  AmbientSensor s(ch, kLightPin, kMeasPeriod);
  setFakeMillis(0U);
  s.init();
  runCycle(s, kMeasPeriod + 1U);

  preloadMeasurement(0x69U, 0x68U, 0x72U, 0xB1U); // second: 25.50°C (diff=50 == tolerance)
  ch.sendCount = 0U;
  runCycle(s, 2U * (kMeasPeriod + 1U));
  IS_EQUAL(ch.sendCount, 0U);
  clearFakeMillis();
  END_IT
}

// ---- humidity exceeds tolerance → sends ----

bool test_hum_change_above_tolerance_triggers_send() {
  IT("humidity change > kHumTolerance (3%) triggers an immediate send");
  preloadInit();
  preloadMeasurement(0x68U, 0xADU, 0x72U, 0xB1U); // first: 50%
  setAnalogReadValue(0U);
  TestCanHandler ch;
  AmbientSensor s(ch, kLightPin, kMeasPeriod);
  setFakeMillis(0U);
  s.init();
  runCycle(s, kMeasPeriod + 1U);

  preloadMeasurement(0x68U, 0xADU, 0x7AU, 0xE2U); // second: 54% (+4 > 3)
  ch.sendCount = 0U;
  runCycle(s, 2U * (kMeasPeriod + 1U));
  IS_EQUAL(ch.sendCount, 1U);
  clearFakeMillis();
  END_IT
}

// ---- 30-minute forced send ----

bool test_forced_send_after_30_minutes() {
  IT("a forced send occurs after 30 minutes even when readings have not changed");
  preloadInit();
  preloadMeasurement(0x68U, 0xADU, 0x72U, 0xB1U);
  setAnalogReadValue(0U);
  TestCanHandler ch;
  AmbientSensor s(ch, kLightPin, kMeasPeriod);
  setFakeMillis(0U);
  s.init();
  runCycle(s, kMeasPeriod + 1U); // first send at t≈101

  preloadMeasurement(0x68U, 0xADU, 0x72U, 0xB1U); // same values
  ch.sendCount = 0U;
  // Simulate 30 min + 1 ms elapsed since last send
  static constexpr uint32_t k30min = 1800000UL;
  runCycle(s, kMeasPeriod + 1U + k30min + 1U);
  IS_EQUAL(ch.sendCount, 1U);
  clearFakeMillis();
  END_IT
}

// ---- sensor error path ----

bool test_sensor_error_sends_error_command() {
  IT("SI7021 read failure transitions to SENSOR_ERROR and sends HUM_TEMP_SENSOR_ERROR");
  preloadInit();
  // Leave queue empty → requestFrom returns 0 → getCelsiusHundredths fails
  setAnalogReadValue(0U);
  TestCanHandler ch;
  AmbientSensor s(ch, kLightPin, kMeasPeriod);
  setFakeMillis(0U);
  s.init();
  runCycle(s, kMeasPeriod + 1U);
  IS_EQUAL(ch.sendCount, 1U);
  IS_EQUAL(ch.lastCommand, static_cast<uint16_t>(CanCmd::HUM_TEMP_SENSOR_ERROR));
  clearFakeMillis();
  END_IT
}

bool test_run_always_returns_true() {
  IT("run() always returns true regardless of state");
  preloadInit();
  TestCanHandler ch;
  AmbientSensor s(ch, kLightPin, kMeasPeriod);
  setFakeMillis(0U);
  s.init();
  IS_TRUE(s.run());
  clearFakeMillis();
  END_IT
}

int main() {
  SUITE("AmbientSensor");
  test_init_fails_when_si7021_not_found();
  test_init_succeeds_when_si7021_present();
  test_no_send_before_measure_period();
  test_first_cycle_sends_values();
  test_second_cycle_same_values_no_send();
  test_temp_change_above_tolerance_triggers_send();
  test_temp_change_at_tolerance_no_send();
  test_hum_change_above_tolerance_triggers_send();
  test_forced_send_after_30_minutes();
  test_sensor_error_sends_error_command();
  test_run_always_returns_true();
  FINISH
}
