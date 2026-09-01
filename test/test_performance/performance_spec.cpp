#include "performance.hpp"
#include "Arduino.h"
#include "BDDTest.h"

static uint32_t callbackCount = 0U;
static uint32_t callbackLastValue = 0U;

static void onMaxRoundTime(uint32_t maxRoundTime) {
  callbackCount++;
  callbackLastValue = maxRoundTime;
}

// ---- init() ----

bool test_init_returns_true() {
  IT("init() always returns true");
  setFakeMillis(0U);
  Performance p(100U, nullptr);
  IS_TRUE(p.init());
  clearFakeMillis();
  END_IT
}

// ---- run() ----

bool test_run_returns_true() {
  IT("run() always returns true");
  setFakeMillis(0U);
  Performance p(100U, nullptr);
  p.init();
  IS_TRUE(p.run());
  clearFakeMillis();
  END_IT
}

bool test_run_no_callback_below_limit() {
  IT("run() does not call callback when round time is below the current maximum");
  setFakeMillis(0U);
  Performance p(100U, onMaxRoundTime);
  p.init(); // lastRunTime = 0
  callbackCount = 0U;
  setFakeMillis(50U);
  p.run(); // delta = 50, 50 < 100 → no callback
  IS_EQUAL(callbackCount, 0U);
  clearFakeMillis();
  END_IT
}

bool test_run_callback_called_on_new_max() {
  IT("run() calls callback with the new max when round time exceeds the current maximum");
  setFakeMillis(0U);
  Performance p(100U, onMaxRoundTime);
  p.init(); // lastRunTime = 0
  callbackCount = 0U;
  callbackLastValue = 0U;
  setFakeMillis(150U);
  p.run(); // delta = 150, 150 > 100 → callback(150)
  IS_EQUAL(callbackCount, 1U);
  IS_EQUAL(callbackLastValue, 150U);
  clearFakeMillis();
  END_IT
}

bool test_run_no_callback_equal_limit() {
  IT("run() does not call callback when round time equals the current maximum (strictly greater required)");
  setFakeMillis(0U);
  Performance p(100U, onMaxRoundTime);
  p.init();
  callbackCount = 0U;
  setFakeMillis(100U);
  p.run(); // delta = 100, 100 > 100 is false → no callback
  IS_EQUAL(callbackCount, 0U);
  clearFakeMillis();
  END_IT
}

bool test_run_null_callback_does_not_crash() {
  IT("run() with a null callback does not crash when a new max is recorded");
  setFakeMillis(0U);
  Performance p(50U, nullptr);
  p.init();
  setFakeMillis(200U);
  IS_TRUE(p.run()); // delta = 200 > 50; callback is null — must not crash
  clearFakeMillis();
  END_IT
}

bool test_run_updates_max_loop_time() {
  IT("run() updates the internal max so a subsequent smaller delta does not trigger callback");
  setFakeMillis(0U);
  Performance p(50U, onMaxRoundTime);
  p.init(); // lastRunTime = 0

  setFakeMillis(200U);
  callbackCount = 0U;
  p.run(); // delta = 200 > 50 → callback, maxRoundTime = 200

  callbackCount = 0U;
  setFakeMillis(350U);
  p.run(); // delta = 150 < 200 → no callback
  IS_EQUAL(callbackCount, 0U);
  clearFakeMillis();
  END_IT
}

// ---- resetTimer() ----

bool test_reset_timer_shifts_reference_point() {
  IT("resetTimer() resets the reference so subsequent run() measures from the new point");
  setFakeMillis(1000U);
  Performance p(50U, onMaxRoundTime);
  p.init(); // lastRunTime = 1000

  setFakeMillis(1200U);
  p.resetTimer(); // lastRunTime = 1200

  callbackCount = 0U;
  setFakeMillis(1230U);
  p.run(); // delta = 30 < 50 → no callback (without resetTimer delta would be 230)
  IS_EQUAL(callbackCount, 0U);
  clearFakeMillis();
  END_IT
}

// ---- multiple-run sequence ----

bool test_multiple_runs_callback_count() {
  IT("callback is invoked exactly once for each new maximum across a sequence of run() calls");
  setFakeMillis(0U);
  Performance p(50U, onMaxRoundTime);
  p.init(); // lastRunTime = 0

  callbackCount = 0U;
  setFakeMillis(30U);
  p.run();  // delta=30  < 50 → no callback; lastRunTime=30
  setFakeMillis(80U);
  p.run();  // delta=50  == 50 → no callback; lastRunTime=80
  setFakeMillis(150U);
  p.run();  // delta=70  > 50  → callback #1 (maxRoundTime=70); lastRunTime=150
  setFakeMillis(200U);
  p.run();  // delta=50  < 70  → no callback; lastRunTime=200
  setFakeMillis(271U);
  p.run();  // delta=71  > 70  → callback #2 (maxRoundTime=71); lastRunTime=271
  IS_EQUAL(callbackCount, 2U);
  clearFakeMillis();
  END_IT
}

// ---- measurement window ----

bool test_a_spike_stops_silencing_the_measurement_after_the_window() {
  IT("a maximum older than the window no longer hides the rounds that follow it");
  setFakeMillis(0U);
  Performance p(50U, onMaxRoundTime);
  p.init();

  callbackCount = 0U;
  setFakeMillis(800U);
  p.run();                                        // delta=800 -> the spike; callback #1
  IS_EQUAL(callbackCount, 1U);
  IS_EQUAL(callbackLastValue, 800U);

  // Ordinary 100 ms rounds from here on: silent while the spike stands, heard once the window
  // has rolled over. Stepping rather than jumping, so no single gap becomes a maximum itself.
  for(uint32_t t = 900U; t <= (16U * 60U * 1000U); t += 100U) {
    setFakeMillis(t);
    p.run();
  }
  IS_EQUAL(callbackCount, 2U);                    // exactly one report after the rollover
  IS_EQUAL(callbackLastValue, 100U);              // and it is the ordinary round, not the old spike
  clearFakeMillis();
  END_IT
}

int main() {
  SUITE("Performance");
  test_init_returns_true();
  test_run_returns_true();
  test_run_no_callback_below_limit();
  test_run_callback_called_on_new_max();
  test_run_no_callback_equal_limit();
  test_run_null_callback_does_not_crash();
  test_run_updates_max_loop_time();
  test_reset_timer_shifts_reference_point();
  test_multiple_runs_callback_count();
  test_a_spike_stops_silencing_the_measurement_after_the_window();
  FINISH
}
