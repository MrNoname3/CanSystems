#include "performance.hpp"
#include "Arduino.h"
#include "BDDTest.h"

static uint32_t callbackCount = 0U;
static uint32_t callbackLastValue = 0U;

static void onMaxLoopTime(uint32_t maxLoopTime) {
  callbackCount++;
  callbackLastValue = maxLoopTime;
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
  IT("run() does not call callback when loop time is below the current maximum");
  setFakeMillis(0U);
  Performance p(100U, onMaxLoopTime);
  p.init(); // lastLoopTime = 0
  callbackCount = 0U;
  setFakeMillis(50U);
  p.run(); // delta = 50, 50 < 100 → no callback
  IS_EQUAL(callbackCount, 0U);
  clearFakeMillis();
  END_IT
}

bool test_run_callback_called_on_new_max() {
  IT("run() calls callback with the new max when loop time exceeds the current maximum");
  setFakeMillis(0U);
  Performance p(100U, onMaxLoopTime);
  p.init(); // lastLoopTime = 0
  callbackCount    = 0U;
  callbackLastValue = 0U;
  setFakeMillis(150U);
  p.run(); // delta = 150, 150 > 100 → callback(150)
  IS_EQUAL(callbackCount,     1U);
  IS_EQUAL(callbackLastValue, 150U);
  clearFakeMillis();
  END_IT
}

bool test_run_no_callback_equal_limit() {
  IT("run() does not call callback when loop time equals the current maximum (strictly greater required)");
  setFakeMillis(0U);
  Performance p(100U, onMaxLoopTime);
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
  Performance p(50U, onMaxLoopTime);
  p.init(); // lastLoopTime = 0

  setFakeMillis(200U);
  callbackCount = 0U;
  p.run(); // delta = 200 > 50 → callback, maxLoopTime = 200

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
  Performance p(50U, onMaxLoopTime);
  p.init(); // lastLoopTime = 1000

  setFakeMillis(1200U);
  p.resetTimer(); // lastLoopTime = 1200

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
  Performance p(50U, onMaxLoopTime);
  p.init(); // lastLoopTime = 0

  callbackCount = 0U;
  setFakeMillis(30U);  p.run();  // delta=30  < 50 → no callback; lastLoopTime=30
  setFakeMillis(80U);  p.run();  // delta=50  == 50 → no callback; lastLoopTime=80
  setFakeMillis(150U); p.run();  // delta=70  > 50  → callback #1 (maxLoopTime=70); lastLoopTime=150
  setFakeMillis(200U); p.run();  // delta=50  < 70  → no callback; lastLoopTime=200
  setFakeMillis(271U); p.run();  // delta=71  > 70  → callback #2 (maxLoopTime=71); lastLoopTime=271
  IS_EQUAL(callbackCount, 2U);
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
  FINISH
}
