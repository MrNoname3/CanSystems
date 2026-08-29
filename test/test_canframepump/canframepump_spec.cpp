#include "canFramePump.hpp"
#include "BDDTest.h"

// A controller stand-in: hands out `pending` frames, then reports empty.
struct FakeController {
  uint8_t pending = 0U;
  uint8_t takeCalls = 0U;
  uint8_t handleCalls = 0U;
  uint8_t rejectAt = 0xFFU;      // Handle call number (1-based) that rejects its frame.

  bool take() {
    ++takeCalls;
    if(pending == 0U) { return false; }
    --pending;
    return true;
  }

  bool handle() {
    ++handleCalls;
    return handleCalls != rejectAt;
  }
};

static CanFramePump::Result runPump(FakeController& controller, uint8_t maxFrames) {
  return CanFramePump::drain([&controller]() -> bool { return controller.take(); },
                             [&controller]() -> bool { return controller.handle(); },
                             maxFrames);
}

bool test_empty_controller_handles_nothing() {
  IT("an empty controller is asked once and nothing is handled");
  FakeController controller;
  const CanFramePump::Result result = runPump(controller, 8U);
  IS_EQUAL(result.handled, 0U);
  IS_FALSE(result.failed);
  IS_EQUAL(controller.takeCalls, 1U);
  IS_EQUAL(controller.handleCalls, 0U);      // no frame -> no dispatch
  END_IT
}

bool test_single_frame_is_handled() {
  IT("a single pending frame is taken and handled");
  FakeController controller;
  controller.pending = 1U;
  const CanFramePump::Result result = runPump(controller, 8U);
  IS_EQUAL(result.handled, 1U);
  IS_FALSE(result.failed);
  IS_EQUAL(controller.takeCalls, 2U);        // one for the frame, one that finds the controller empty
  END_IT
}

bool test_every_pending_frame_is_drained_in_one_pass() {
  IT("all pending frames are drained in a single pass");
  FakeController controller;
  controller.pending = 3U;
  const CanFramePump::Result result = runPump(controller, 8U);
  IS_EQUAL(result.handled, 3U);
  IS_EQUAL(controller.pending, 0U);
  IS_EQUAL(controller.takeCalls, 4U);
  END_IT
}

bool test_the_frame_budget_bounds_one_pass() {
  IT("the frame budget stops the pass and leaves the rest for the next one");
  FakeController controller;
  controller.pending = 10U;
  const CanFramePump::Result result = runPump(controller, 4U);
  IS_EQUAL(result.handled, 4U);
  IS_FALSE(result.failed);
  IS_EQUAL(controller.pending, 6U);          // the burst cannot starve the other tasks
  END_IT
}

bool test_a_zero_budget_takes_nothing() {
  IT("a zero frame budget touches the controller at all");
  FakeController controller;
  controller.pending = 5U;
  const CanFramePump::Result result = runPump(controller, 0U);
  IS_EQUAL(result.handled, 0U);
  IS_EQUAL(controller.takeCalls, 0U);
  IS_EQUAL(controller.pending, 5U);
  END_IT
}

bool test_a_rejected_frame_stops_the_pass() {
  IT("a rejected frame ends the pass and is reported");
  FakeController controller;
  controller.pending = 3U;
  controller.rejectAt = 2U;                  // the second frame fails to handle
  const CanFramePump::Result result = runPump(controller, 8U);
  IS_EQUAL(result.handled, 1U);
  IS_TRUE(result.failed);
  IS_EQUAL(controller.handleCalls, 2U);
  IS_EQUAL(controller.pending, 1U);          // the untouched frame stays for the next pass
  END_IT
}

int main() {
  SUITE("CanFramePump");
  test_empty_controller_handles_nothing();
  test_single_frame_is_handled();
  test_every_pending_frame_is_drained_in_one_pass();
  test_the_frame_budget_bounds_one_pass();
  test_a_zero_budget_takes_nothing();
  test_a_rejected_frame_stops_the_pass();
  FINISH
}
