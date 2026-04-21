#include "PushButtonClicks.hpp"
#include "BDDTest.h"

// PushButton(deadTime=100, longPressTime=500, debounceTime=70, polarity=false)
// polarity=false: button pressed when pin reads false (LOW)
static constexpr uint8_t  kDead     = 100U;
static constexpr uint16_t kLong     = 500U;
static constexpr uint8_t  kDebounce = 70U;
static constexpr bool     kPol      = false;

static PushButton make() { return PushButton(kDead, kLong, kDebounce, kPol); }

// ---- idle / no event ----

bool test_idle_returns_zero() {
  IT("returns 0 when button is never pressed");
  PushButton btn = make();
  IS_EQUAL(btn.buttonCheck(0U,   true), 0U);
  IS_EQUAL(btn.buttonCheck(500U, true), 0U);
  END_IT
}

// ---- debounce ----

bool test_bounce_below_debounce_no_event() {
  IT("press shorter than debounceTime produces no event");
  PushButton btn = make();
  btn.buttonCheck(0U,  false); // press
  btn.buttonCheck(50U, true);  // release after 50 ms < 70 ms debounce
  IS_EQUAL(btn.buttonCheck(200U, true), 0U); // deadTime passed — still nothing
  END_IT
}

bool test_press_exactly_debounce_no_event() {
  IT("press of exactly debounceTime (not strictly greater) produces no event");
  PushButton btn = make();
  btn.buttonCheck(0U,  false);
  btn.buttonCheck(70U, true);  // pressedDuration == 70, not > 70
  IS_EQUAL(btn.buttonCheck(250U, true), 0U);
  END_IT
}

// ---- single tap ----

bool test_single_tap_returns_3() {
  IT("single tap returns 3 (ONE_PRESS) after dead time elapses");
  PushButton btn = make();
  btn.buttonCheck(0U,   false); // press
  btn.buttonCheck(71U,  false); // still held at 71 ms
  btn.buttonCheck(100U, true);  // release; pressedDuration=100 > 70 → shortPressedCnt=3
  IS_EQUAL(btn.buttonCheck(201U, true), 3U); // 201-100=101 > deadTime → output=3
  END_IT
}

bool test_single_tap_no_event_before_dead_time() {
  IT("single tap returns 0 while dead time has not elapsed");
  PushButton btn = make();
  btn.buttonCheck(0U,   false);
  btn.buttonCheck(80U,  false); // accumulate 80 ms > debounce 70 ms
  btn.buttonCheck(90U,  true);  // release; shortPressedCnt=3, lastEventTime=90
  IS_EQUAL(btn.buttonCheck(150U, true), 0U); // 150-90=60 < deadTime 100 → no output yet
  END_IT
}

// ---- double tap ----

bool test_double_tap_returns_4() {
  IT("double tap returns 4 (TWO_PRESS)");
  PushButton btn = make();
  // 1st press: hold for 80 ms (> debounce 70 ms)
  btn.buttonCheck(0U,   false);
  btn.buttonCheck(80U,  false); // accumulate 80 ms
  btn.buttonCheck(90U,  true);  // 1st release; shortPressedCnt=3, lastEventTime=90
  // 2nd press
  btn.buttonCheck(110U, false);
  btn.buttonCheck(190U, false); // accumulate 80 ms
  btn.buttonCheck(200U, true);  // 2nd release; shortPressedCnt=4, lastEventTime=200
  IS_EQUAL(btn.buttonCheck(301U, true), 4U); // 301-200=101 > deadTime 100 → output=4
  END_IT
}

bool test_triple_tap_returns_5() {
  IT("triple tap returns 5 (THREE_PRESS)");
  PushButton btn = make();
  btn.buttonCheck(0U,   false);
  btn.buttonCheck(80U,  false); btn.buttonCheck(90U,  true);  // 1st release; cnt=3
  btn.buttonCheck(110U, false);
  btn.buttonCheck(190U, false); btn.buttonCheck(200U, true);  // 2nd release; cnt=4
  btn.buttonCheck(220U, false);
  btn.buttonCheck(300U, false); btn.buttonCheck(310U, true);  // 3rd release; cnt=5, lastEventTime=310
  IS_EQUAL(btn.buttonCheck(411U, true), 5U); // 411-310=101 > 100 → output=5
  END_IT
}

// ---- long press ----

bool test_long_press_returns_1() {
  IT("long press (no release) returns 1 once threshold exceeded");
  PushButton btn = make();
  btn.buttonCheck(0U,   false);
  btn.buttonCheck(300U, false);
  IS_EQUAL(btn.buttonCheck(501U, false), 1U); // pressedDuration=501 > 500 → output=1
  END_IT
}

bool test_long_press_fires_once() {
  IT("long press event fires exactly once during continued hold");
  PushButton btn = make();
  btn.buttonCheck(0U,   false);
  btn.buttonCheck(501U, false); // first fire
  IS_EQUAL(btn.buttonCheck(600U, false), 0U); // longPressflag prevents second fire
  END_IT
}

// ---- long release ----

bool test_long_release_returns_2() {
  IT("releasing after a long press returns 2 (LONG_RELEASE)");
  PushButton btn = make();
  btn.buttonCheck(0U,   false);
  btn.buttonCheck(600U, false); // long press event (fires 1)
  IS_EQUAL(btn.buttonCheck(700U, true), 2U); // release → output=2
  END_IT
}

bool test_long_release_resets_short_count() {
  IT("long release resets the short press counter to baseline");
  PushButton btn = make();
  btn.buttonCheck(0U,   false);
  btn.buttonCheck(600U, false); // accumulate 600 ms → triggers long press event (1)
  btn.buttonCheck(700U, true);  // release; 600 > 500 → long release (2), shortPressedCnt=2
  // shortPressedCnt is now 2 (baseline) → no spurious short-tap event
  IS_EQUAL(btn.buttonCheck(900U, true), 0U);
  END_IT
}

// ---- polarity HIGH ----

bool test_high_polarity_pressed_when_high() {
  IT("with HIGH polarity, button pressed when pin reads true");
  PushButton btn(kDead, kLong, kDebounce, true); // polarity = HIGH
  btn.buttonCheck(0U,   true);  // press
  btn.buttonCheck(80U,  true);  // still pressed; accumulate 80 ms > debounce 70 ms
  btn.buttonCheck(90U,  false); // release; shortPressedCnt=3, lastEventTime=90
  IS_EQUAL(btn.buttonCheck(191U, false), 3U); // 191-90=101 > deadTime 100 → output=3
  END_IT
}

int main() {
  SUITE("PushButton");
  test_idle_returns_zero();
  test_bounce_below_debounce_no_event();
  test_press_exactly_debounce_no_event();
  test_single_tap_returns_3();
  test_single_tap_no_event_before_dead_time();
  test_double_tap_returns_4();
  test_triple_tap_returns_5();
  test_long_press_returns_1();
  test_long_press_fires_once();
  test_long_release_returns_2();
  test_long_release_resets_short_count();
  test_high_polarity_pressed_when_high();
  FINISH
}
