#include "rtcStore.hpp"
#include "BDDTest.h"

// The store the reconnect backoff and the startup progress share: what a slot reads back, and how
// a slot nothing has written tells itself apart from one holding a zero.

bool test_an_unwritten_slot_reads_as_absent() {
  IT("a slot no run has written reports nothing rather than a zero");
  uint8_t value = 42U;
  IS_FALSE(RtcStore::read(RtcStore::Slot::BackoffStep, value));
  IS_EQUAL(value, 42U);   // The caller's value is left alone.
  END_IT
}

bool test_a_written_slot_reads_back() {
  IT("a slot reads back what was written to it");
  RtcStore::write(RtcStore::Slot::BackoffStep, 3U);
  uint8_t value = 0U;
  IS_TRUE(RtcStore::read(RtcStore::Slot::BackoffStep, value));
  IS_EQUAL(value, 3U);
  END_IT
}

bool test_writing_one_slot_leaves_the_others_absent() {
  IT("writing one slot does not make an untouched neighbour look written");
  // The startup progress writes early; the backoff must still see that nothing left it a rung,
  // or a power-on start would sit out a wait it never earned.
  uint8_t value = 0U;
  IS_FALSE(RtcStore::read(RtcStore::Slot::BootStage, value));
  END_IT
}

bool test_slots_keep_their_own_values() {
  IT("each slot keeps its own value");
  RtcStore::write(RtcStore::Slot::BootStage, 9U);
  RtcStore::write(RtcStore::Slot::BackoffStep, 5U);
  uint8_t stage = 0U;
  uint8_t step = 0U;
  IS_TRUE(RtcStore::read(RtcStore::Slot::BootStage, stage));
  IS_TRUE(RtcStore::read(RtcStore::Slot::BackoffStep, step));
  IS_EQUAL(stage, 9U);
  IS_EQUAL(step, 5U);
  END_IT
}

bool test_a_slot_can_be_overwritten() {
  IT("writing a slot again replaces its value");
  RtcStore::write(RtcStore::Slot::BootStage, 12U);
  uint8_t stage = 0U;
  IS_TRUE(RtcStore::read(RtcStore::Slot::BootStage, stage));
  IS_EQUAL(stage, 12U);
  END_IT
}

int main() {
  SUITE("RtcStore");
  // Order matters: the first two cases rely on the store starting out empty.
  test_an_unwritten_slot_reads_as_absent();
  test_a_written_slot_reads_back();
  test_writing_one_slot_leaves_the_others_absent();
  test_slots_keep_their_own_values();
  test_a_slot_can_be_overwritten();
  FINISH
}
