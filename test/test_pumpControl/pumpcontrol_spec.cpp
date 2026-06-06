#include "pumpControl.hpp"
#include "Arduino.h"
#include "Wire.h"
#include "BDDTest.h"

static constexpr uint8_t kPwmPin     = 5U;
static constexpr uint8_t kIntPin     = 3U;
static constexpr uint8_t kCurrentPin = 7U;

// PumpControl error codes (mirror of the private PumpControlError enum bit positions).
static constexpr uint8_t kErrChSelect   = 1U << 0U;
static constexpr uint8_t kErrPumpOverrun = 1U << 3U;
static constexpr uint8_t kErrQueueFull   = 1U << 6U;

// ---- reportError capture ----
static uint32_t g_lastErr;
static uint32_t g_errCount;
static void onError(uint8_t code) { g_lastErr = code; ++g_errCount; }
static void resetErr() { g_lastErr = 0U; g_errCount = 0U; }

// Brings a fresh PumpControl from CALIBRATION to IDLE. The convergence loop runs at millis()==0
// so handleCalibration() never fires (the 5 s window is not elapsed) and analogValue settles near
// 'adc'; the final run() at 6 s completes calibration and transitions to IDLE.
static void driveToIdle(PumpControl& pc, uint16_t adc) {
  setAnalogReadValue(adc);
  setFakeMillis(0U);
  (void)pc.init();
  for(uint8_t i = 0U; i < 120U; ++i) { (void)pc.run(); }
  setFakeMillis(6000U);
  (void)pc.run();
}

// Applies exactly one current-sense filter step from a zeroed analogValue, so the resulting
// analogValue is the deterministic floor(25*adc/255). Stays in CALIBRATION (millis()==0), so no
// other state-machine side effects run.
static void oneFilterStep(PumpControl& pc, uint16_t adc) {
  setAnalogReadValue(adc);
  setFakeMillis(0U);
  (void)pc.init();
  (void)pc.run();
}

// ---- calculateCurrent / getPositiveCurrent (ACS712-5A math) ----
// After oneFilterStep the analogValue is floor(25*adc/255):
//   sensorVoltageMV = analogValue * 5000 / 1024
//   currentMA       = (sensorVoltageMV - 2500) * 1000 / 185

bool test_current_positive() {
  IT("calculateCurrent converts a high reading to a positive mA value");
  resetErr();
  Wire.reset();
  PCF8574 pcf(5000U, 0x27U);
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  oneFilterStep(pc, 6120U);              // analogValue = 600
  IS_EQUAL(pc.calculateCurrent(), 2318); // (2929 - 2500) * 1000 / 185
  IS_EQUAL(pc.getPositiveCurrent(), 2318U);
  clearFakeMillis();
  END_IT
}

bool test_current_zero_reading_is_negative() {
  IT("calculateCurrent is negative for a zero reading and getPositiveCurrent clamps it to 0");
  resetErr();
  Wire.reset();
  PCF8574 pcf(5000U, 0x27U);
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  oneFilterStep(pc, 0U);                  // analogValue = 0
  IS_EQUAL(pc.calculateCurrent(), -13513);
  IS_EQUAL(pc.getPositiveCurrent(), 0U);  // negatives clamp to 0
  clearFakeMillis();
  END_IT
}

bool test_current_just_below_zero_clamps() {
  IT("getPositiveCurrent clamps a small negative current to 0");
  resetErr();
  Wire.reset();
  PCF8574 pcf(5000U, 0x27U);
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  oneFilterStep(pc, 5222U);               // analogValue = 511 -> -27 mA
  IS_EQUAL(pc.calculateCurrent(), -27);
  IS_EQUAL(pc.getPositiveCurrent(), 0U);
  clearFakeMillis();
  END_IT
}

bool test_run_returns_true() {
  IT("run() returns true");
  resetErr();
  Wire.reset();
  PCF8574 pcf(5000U, 0x27U);
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  setFakeMillis(0U);
  (void)pc.init();
  IS_TRUE(pc.run());
  clearFakeMillis();
  END_IT
}

// ---- safety checks in IDLE ----

bool test_idle_reports_pump_overrun_on_standby_current() {
  IT("an idle pump drawing current above the standby limit reports PUMP_OVERRUN");
  resetErr();
  Wire.reset();
  PCF8574 pcf(5000U, 0x27U);
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  driveToIdle(pc, 800U);                  // analogValue settles high -> current >> 100 mA standby limit
  resetErr();
  setFakeMillis(6000U);
  (void)pc.run();                         // IDLE, empty queue -> overrun detected
  IS_EQUAL(g_lastErr, static_cast<uint32_t>(kErrPumpOverrun));
  clearFakeMillis();
  END_IT
}

// ---- queue overflow ----

bool test_queue_full_is_reported() {
  IT("queuing more irrigations than the buffer holds reports QUEUE_FULL");
  resetErr();
  Wire.reset();
  Wire.setEndTransmissionResult(0U);
  PCF8574 pcf(5000U, 0x27U);
  (void)pcf.init();                       // channel select writes must succeed
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  driveToIdle(pc, 512U);                  // ~0 mA current -> no spurious overrun

  // Queue capacity is the channel count (4); the 5th enqueue overflows.
  for(uint8_t i = 0U; i < 5U; ++i) {
    pc.createIrrigation(0U, 1U, false, false, 100U, 0U);
  }
  resetErr();
  setFakeMillis(6000U);
  (void)pc.run();                         // IDLE dispatches queue; QUEUE_FULL error is flushed
  IS_EQUAL(g_lastErr, static_cast<uint32_t>(kErrQueueFull));
  clearFakeMillis();
  END_IT
}

int main() {
  SUITE("PumpControl");
  test_current_positive();
  test_current_zero_reading_is_negative();
  test_current_just_below_zero_clamps();
  test_run_returns_true();
  test_idle_reports_pump_overrun_on_standby_current();
  test_queue_full_is_reported();
  FINISH
}
