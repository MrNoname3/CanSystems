#include "pumpControl.hpp"
#include "Arduino.h"
#include "Wire.h"
#include "BDDTest.h"

static constexpr uint8_t kPwmPin     = 5U;
static constexpr uint8_t kIntPin     = 3U;
static constexpr uint8_t kCurrentPin = 7U;

// PumpControl error codes (mirror of the private PumpControlError enum bit positions).
static constexpr uint8_t kErrChSelect    = 1U << 0U;
static constexpr uint8_t kErrFlowStuck    = 1U << 1U;
static constexpr uint8_t kErrPumpOverrun = 1U << 3U;
static constexpr uint8_t kErrPumpOc       = 1U << 4U;
static constexpr uint8_t kErrPumpUc       = 1U << 5U;
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

// ---- irrigation state machine ----
// Boilerplate note: each test inits the PCF (so selectChannel writes succeed) and uses
// driveToIdle(pc, 512U) to reach IDLE with ~0 mA sensed current, then resets the error capture.

bool test_full_irrigation_cycle() {
  IT("an irrigation runs at its PWM then stops and powers the pump off after its duration");
  Wire.reset();
  Wire.setEndTransmissionResult(0U);
  PCF8574 pcf(5000U, 0x27U);
  (void)pcf.init();
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  driveToIdle(pc, 512U);
  resetErr();
  pc.createIrrigation(0U, 1U, false, false, 200U, 0U);  // channel 0, 1 min, PWM 200
  setFakeMillis(6000U);          (void)pc.run();         // IDLE -> RUN
  IS_EQUAL(getDigitalWriteValue(kPwmPin), 200U);
  setFakeMillis(6000U + 60001U); (void)pc.run();         // RUN -> STOP (duration elapsed)
  (void)pc.run();                                        // STOP -> IDLE (queue empty -> pump off)
  IS_EQUAL(getDigitalWriteValue(kPwmPin), 0U);
  IS_EQUAL(g_errCount, 0U);
  clearFakeMillis();
  END_IT
}

bool test_encoded_irrigation_selects_channel() {
  IT("createIrrigation(encoded) selects the requested PCF channel");
  Wire.reset();
  Wire.setEndTransmissionResult(0U);
  PCF8574 pcf(5000U, 0x27U);
  (void)pcf.init();
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  driveToIdle(pc, 512U);
  resetErr();
  pc.createIrrigation(2U, 200U, 0U);     // encoded info: channel 2 (bits 0-1)
  setFakeMillis(6000U); (void)pc.run();  // IDLE -> RUN: selectChannel(2)
  IS_EQUAL(pcf.getRegisterValue(), 0xF4U);  // high nibble kept, bit 2 set
  clearFakeMillis();
  END_IT
}

bool test_channel_select_failure_reports_error() {
  IT("a failing channel select (PCF not present) reports CH_SELECT");
  Wire.reset();
  PCF8574 pcf(5000U, 0x27U);     // NOT initialised -> write() fails
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  driveToIdle(pc, 512U);
  resetErr();
  pc.createIrrigation(0U, 1U, false, false, 200U, 0U);
  setFakeMillis(6000U); (void)pc.run();   // IDLE: selectChannel fails -> CH_SELECT (reported same call)
  IS_EQUAL(g_lastErr, static_cast<uint32_t>(kErrChSelect));
  clearFakeMillis();
  END_IT
}

bool test_flow_stuck_reports_error() {
  IT("a flow-checked irrigation with no pulses reports FLOW_STUCK");
  Wire.reset();
  Wire.setEndTransmissionResult(0U);
  PCF8574 pcf(5000U, 0x27U);
  (void)pcf.init();
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  driveToIdle(pc, 512U);
  resetErr();
  pc.createIrrigation(0U, 10U, true, false, 200U, 0U);  // checkFlow = true, long duration
  setFakeMillis(6000U);          (void)pc.run();         // IDLE -> RUN
  setFakeMillis(6000U + 1001U);  (void)pc.run();         // RUN: error check -> no flow -> FLOW_STUCK, ERROR
  setFakeMillis(6000U + 1001U);  (void)pc.run();         // ERROR -> IDLE
  setFakeMillis(6000U + 1001U);  (void)pc.run();         // IDLE -> error flushed
  IS_EQUAL(g_lastErr, static_cast<uint32_t>(kErrFlowStuck));
  clearFakeMillis();
  END_IT
}

bool test_undercurrent_reports_error() {
  IT("a current-checked irrigation drawing too little current reports PUMP_UC");
  Wire.reset();
  Wire.setEndTransmissionResult(0U);
  PCF8574 pcf(5000U, 0x27U);
  (void)pcf.init();
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  driveToIdle(pc, 512U);          // ~0 mA, below the 100 mA standby threshold
  resetErr();
  pc.createIrrigation(0U, 10U, false, true, 200U, 0U);  // checkCurrent = true
  setFakeMillis(6000U);          (void)pc.run();         // RUN
  setFakeMillis(6000U + 1001U);  (void)pc.run();         // RUN: current < standby -> PUMP_UC, ERROR
  setFakeMillis(6000U + 1001U);  (void)pc.run();         // ERROR -> IDLE
  setFakeMillis(6000U + 1001U);  (void)pc.run();         // IDLE -> error flushed
  IS_EQUAL(g_lastErr, static_cast<uint32_t>(kErrPumpUc));
  clearFakeMillis();
  END_IT
}

bool test_overcurrent_reports_error() {
  IT("a current-checked irrigation drawing too much current reports PUMP_OC");
  Wire.reset();
  Wire.setEndTransmissionResult(0U);
  PCF8574 pcf(5000U, 0x27U);
  (void)pcf.init();
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  driveToIdle(pc, 512U);
  resetErr();
  pc.createIrrigation(0U, 10U, false, true, 200U, 0U);  // checkCurrent = true
  setAnalogReadValue(1023U);                             // drive sensed current well over 1000 mA
  setFakeMillis(6000U);          (void)pc.run();         // RUN (filter raises analogValue)
  setFakeMillis(6000U + 1001U);  (void)pc.run();         // RUN: current > max -> PUMP_OC, ERROR
  setFakeMillis(6000U + 1001U);  (void)pc.run();         // ERROR -> IDLE
  setFakeMillis(6000U + 1001U);  (void)pc.run();         // IDLE -> error flushed
  IS_TRUE((g_lastErr & static_cast<uint32_t>(kErrPumpOc)) != 0U);
  clearFakeMillis();
  END_IT
}

bool test_skip_actual_irrigation_stops_run() {
  IT("skipActualIrrigation() ends the running irrigation early");
  Wire.reset();
  Wire.setEndTransmissionResult(0U);
  PCF8574 pcf(5000U, 0x27U);
  (void)pcf.init();
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  driveToIdle(pc, 512U);
  resetErr();
  pc.createIrrigation(0U, 10U, false, false, 200U, 0U);
  setFakeMillis(6000U); (void)pc.run();    // RUN
  IS_EQUAL(getDigitalWriteValue(kPwmPin), 200U);
  pc.skipActualIrrigation();               // backdates the timer; fires once time advances
  setFakeMillis(6001U); (void)pc.run();    // RUN -> STOP
  (void)pc.run();                          // STOP -> IDLE (pump off)
  IS_EQUAL(getDigitalWriteValue(kPwmPin), 0U);
  clearFakeMillis();
  END_IT
}

bool test_skip_all_irrigations_clears_queue() {
  IT("skipAllIrrigations() aborts the run and powers the pump off");
  Wire.reset();
  Wire.setEndTransmissionResult(0U);
  PCF8574 pcf(5000U, 0x27U);
  (void)pcf.init();
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  driveToIdle(pc, 512U);
  resetErr();
  pc.createIrrigation(0U, 10U, false, false, 200U, 0U);
  setFakeMillis(6000U); (void)pc.run();    // RUN
  pc.skipAllIrrigations();                 // clears queue, forces ERROR
  setFakeMillis(6000U); (void)pc.run();    // ERROR -> IDLE (pump off)
  IS_EQUAL(getDigitalWriteValue(kPwmPin), 0U);
  IS_EQUAL(g_errCount, 0U);                // abort is not an error report
  clearFakeMillis();
  END_IT
}

bool test_limit_switch_stops_run() {
  IT("a triggered limit switch stops the irrigation");
  Wire.reset();
  Wire.setEndTransmissionResult(0U);
  PCF8574 pcf(5000U, 0x27U);
  (void)pcf.init();
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  driveToIdle(pc, 512U);
  resetErr();
  pc.addLimitSwitch(0U, []() -> bool { return true; });  // always "reached"
  pc.createIrrigation(0U, 10U, false, false, 200U, 0U);
  setFakeMillis(6000U); (void)pc.run();    // IDLE -> RUN
  setFakeMillis(6000U); (void)pc.run();    // RUN: limit reached -> STOP
  (void)pc.run();                          // STOP -> IDLE (pump off)
  IS_EQUAL(getDigitalWriteValue(kPwmPin), 0U);
  IS_EQUAL(g_errCount, 0U);
  clearFakeMillis();
  END_IT
}

bool test_repeat_irrigation_runs_again() {
  IT("an irrigation with repeatNum re-runs after its first cycle");
  Wire.reset();
  Wire.setEndTransmissionResult(0U);
  PCF8574 pcf(5000U, 0x27U);
  (void)pcf.init();
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  driveToIdle(pc, 512U);
  resetErr();
  pc.createIrrigation(0U, 1U, false, false, 200U, 1U);  // repeat once
  setFakeMillis(6000U);           (void)pc.run();        // RUN
  setFakeMillis(66001U);          (void)pc.run();        // RUN -> STOP (1 min)
  setFakeMillis(66001U);          (void)pc.run();        // STOP: repeat -> re-queue (same channel, still on)
  IS_EQUAL(getDigitalWriteValue(kPwmPin), 200U);
  setFakeMillis(66001U);          (void)pc.run();        // IDLE -> RUN (repeat starts)
  setFakeMillis(66001U + 60001U); (void)pc.run();        // RUN -> STOP (2nd min)
  (void)pc.run();                                        // STOP -> IDLE (repeat exhausted -> off)
  IS_EQUAL(getDigitalWriteValue(kPwmPin), 0U);
  clearFakeMillis();
  END_IT
}

bool test_safety_irrigation_triggers() {
  IT("a safety irrigation starts automatically once its time has elapsed");
  Wire.reset();
  Wire.setEndTransmissionResult(0U);
  PCF8574 pcf(5000U, 0x27U);
  (void)pcf.init();
  RgbLedWrapper led(1U, 6U);
  PumpControl pc(pcf, led, kPwmPin, kIntPin, kCurrentPin, onError);
  driveToIdle(pc, 512U);                                 // millis() == 6000
  resetErr();
  pc.addSafetyIrrigation(1U, 0U, 1U, false, false, 200U, 0U);  // every 1 min, channel 0
  setFakeMillis(6000U + 60001U); (void)pc.run();         // IDLE: safety window elapsed -> enqueues
  (void)pc.run();                                        // IDLE -> RUN (safety irrigation starts)
  IS_EQUAL(getDigitalWriteValue(kPwmPin), 200U);
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
  test_full_irrigation_cycle();
  test_encoded_irrigation_selects_channel();
  test_channel_select_failure_reports_error();
  test_flow_stuck_reports_error();
  test_undercurrent_reports_error();
  test_overcurrent_reports_error();
  test_skip_actual_irrigation_stops_run();
  test_skip_all_irrigations_clears_queue();
  test_limit_switch_stops_run();
  test_repeat_irrigation_runs_again();
  test_safety_irrigation_triggers();
  FINISH
}
