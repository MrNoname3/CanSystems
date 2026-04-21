#include "pushButtonHandler.hpp"
#include "Arduino.h"
#include "BDDTest.h"
#include <string.h>

// ---- TestCanHandler mock ----

class TestCanHandler final : public CanHandlerBase {
public:
  using CanHandlerBase::send;
  mutable uint32_t sendCount    = 0U;
  mutable uint16_t lastCommand  = 0U;
  mutable uint8_t  lastData[8]  = {};

  bool init() override { return true; } // NOLINT(readability-make-member-function-const)
  bool run()  override { return true; } // NOLINT(readability-make-member-function-const)

  bool send(uint16_t command, const uint8_t (&data)[8]) const override {
    sendCount++;
    lastCommand = command;
    memcpy(lastData, data, 8U);
    return true;
  }
};

// ---- Button state helpers ----
static bool gButtonState = false;
static bool readBtn() { return gButtonState; }

// ---- Callback helpers ----
static uint32_t cbCount = 0U;
static PushButtonHandler::BtnEvent cbLastEvent = PushButtonHandler::BtnEvent::NONE;
static void onBtnEvent(PushButtonHandler::BtnEvent e) { cbCount++; cbLastEvent = e; }

// Helper: drive a qualifying single tap through the handler
// Uses PushButtonHandler constants: deadTime=250, longPressTime=500, debounceTime=70, polarity=false
static void doSingleTap(PushButtonHandler& h) {
  gButtonState = false;           // press (polarity=false → active LOW)
  setFakeMillis(0U);   h.run();
  setFakeMillis(80U);  h.run();   // held 80 ms > debounce 70 ms
  gButtonState = true;            // release
  setFakeMillis(100U); h.run();   // pressedDuration=100 > debounce → shortPressedCnt=3
  setFakeMillis(351U); h.run();   // 351-100=251 > deadTime=250 → fires event=3
}

static void doLongPress(PushButtonHandler& h) {
  gButtonState = false;
  setFakeMillis(0U);   h.run();
  setFakeMillis(501U); h.run();   // pressedDuration=501 > longPressTime=500 → event=1
}

// ---- init ----

bool test_init_returns_true() {
  IT("init() always returns true");
  TestCanHandler ch;
  PushButtonHandler h(ch, readBtn);
  IS_TRUE(h.init());
  END_IT
}

// ---- null reader ----

bool test_null_reader_does_not_crash() {
  IT("run() with null buttonReader returns true without crash");
  TestCanHandler ch;
  PushButtonHandler h(ch, nullptr);
  setFakeMillis(0U);
  IS_TRUE(h.run());
  clearFakeMillis();
  END_IT
}

bool test_null_reader_sends_nothing() {
  IT("run() with null buttonReader sends no CAN frame");
  TestCanHandler ch;
  PushButtonHandler h(ch, nullptr);
  setFakeMillis(0U);
  h.run();
  IS_EQUAL(ch.sendCount, 0U);
  clearFakeMillis();
  END_IT
}

// ---- no event ----

bool test_no_event_when_button_not_pressed() {
  IT("no CAN frame is sent when button stays released");
  TestCanHandler ch;
  PushButtonHandler h(ch, readBtn);
  gButtonState = true; // released (polarity=false)
  setFakeMillis(0U);  h.run();
  setFakeMillis(1000U); h.run();
  IS_EQUAL(ch.sendCount, 0U);
  clearFakeMillis();
  END_IT
}

// ---- single tap → CAN ----

bool test_single_tap_sends_can_event_3() {
  IT("single tap sends CAN BUTTON_EVENT with data[0]=3");
  TestCanHandler ch;
  PushButtonHandler h(ch, readBtn);
  doSingleTap(h);
  IS_EQUAL(ch.sendCount,   1U);
  IS_EQUAL(ch.lastData[0], 3U);
  clearFakeMillis();
  END_IT
}

// ---- long press → CAN ----

bool test_long_press_sends_can_event_1() {
  IT("long press sends CAN BUTTON_EVENT with data[0]=1");
  TestCanHandler ch;
  PushButtonHandler h(ch, readBtn);
  doLongPress(h);
  IS_EQUAL(ch.sendCount,   1U);
  IS_EQUAL(ch.lastData[0], 1U);
  clearFakeMillis();
  END_IT
}

// ---- callback ----

bool test_single_tap_invokes_callback_with_one_press() {
  IT("single tap invokes callback with BtnEvent::ONE_PRESS");
  TestCanHandler ch;
  PushButtonHandler h(ch, readBtn);
  cbCount = 0U;
  h.addBtnCallback(onBtnEvent);
  doSingleTap(h);
  IS_EQUAL(cbCount, 1U);
  IS_EQUAL(static_cast<uint8_t>(cbLastEvent),
           static_cast<uint8_t>(PushButtonHandler::BtnEvent::ONE_PRESS));
  clearFakeMillis();
  END_IT
}

bool test_no_callback_when_not_set() {
  IT("no callback is invoked when addBtnCallback was never called");
  TestCanHandler ch;
  PushButtonHandler h(ch, readBtn);
  cbCount = 0U;
  doSingleTap(h);
  IS_EQUAL(cbCount, 0U);
  clearFakeMillis();
  END_IT
}

bool test_run_still_returns_true() {
  IT("run() always returns true");
  TestCanHandler ch;
  PushButtonHandler h(ch, readBtn);
  gButtonState = true;
  setFakeMillis(0U);
  IS_TRUE(h.run());
  clearFakeMillis();
  END_IT
}

int main() {
  SUITE("PushButtonHandler");
  test_init_returns_true();
  test_null_reader_does_not_crash();
  test_null_reader_sends_nothing();
  test_no_event_when_button_not_pressed();
  test_single_tap_sends_can_event_3();
  test_long_press_sends_can_event_1();
  test_single_tap_invokes_callback_with_one_press();
  test_no_callback_when_not_set();
  test_run_still_returns_true();
  FINISH
}
