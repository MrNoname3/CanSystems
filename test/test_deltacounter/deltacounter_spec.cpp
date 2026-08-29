#include "deltaCounter.hpp"
#include "BDDTest.h"

// The counters this tracks are written by an interrupt and only ever grow, so what a reporter
// needs is "how much since I last looked" - including across the counter's 32-bit wrap.

bool test_the_first_look_reports_everything_counted_so_far() {
  IT("the first look reports the whole count");
  DeltaCounter counter;
  IS_EQUAL(counter.takeGrowth(7U), 7U);
  END_IT
}

bool test_a_counter_that_has_not_moved_reports_nothing() {
  IT("a counter that has not moved reports no growth");
  DeltaCounter counter;
  IS_EQUAL(counter.takeGrowth(7U), 7U);
  IS_EQUAL(counter.takeGrowth(7U), 0U);        // nothing new happened: stays quiet
  IS_EQUAL(counter.takeGrowth(7U), 0U);
  END_IT
}

bool test_each_look_reports_only_what_is_new() {
  IT("each look reports only the growth since the previous one");
  DeltaCounter counter;
  IS_EQUAL(counter.takeGrowth(3U), 3U);
  IS_EQUAL(counter.takeGrowth(5U), 2U);
  IS_EQUAL(counter.takeGrowth(105U), 100U);
  END_IT
}

bool test_growth_survives_the_counter_wrapping() {
  IT("growth is still right when the counter wraps past its maximum");
  DeltaCounter counter;
  IS_EQUAL(counter.takeGrowth(0xFFFFFFFEU), 0xFFFFFFFEU);
  // The interrupt counter is free-running and never reset, so it eventually wraps. Unsigned
  // subtraction gives the true growth across that point; a signed difference would not.
  IS_EQUAL(counter.takeGrowth(2U), 4U);
  END_IT
}

int main() {
  SUITE("DeltaCounter");
  test_the_first_look_reports_everything_counted_so_far();
  test_a_counter_that_has_not_moved_reports_nothing();
  test_each_look_reports_only_what_is_new();
  test_growth_survives_the_counter_wrapping();
  FINISH
}
