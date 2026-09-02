#include "reconnectBackoff.hpp"
#include "BDDTest.h"

// The growing wait between reconnect attempts: how far it climbs, when it starts over, and what it
// makes of a rung carried across a reset.

bool test_the_first_attempt_uses_the_first_rung() {
  IT("a fresh backoff waits the shortest delay");
  ReconnectBackoff backoff;
  IS_EQUAL(backoff.getStepIndex(), 0U);
  IS_EQUAL(backoff.getDelayMs(), ReconnectBackoff::delaysMs[0]);
  END_IT
}

bool test_each_failure_climbs_one_rung() {
  IT("every failed attempt moves one rung up the ladder");
  ReconnectBackoff backoff;
  backoff.onFailure();
  IS_EQUAL(backoff.getDelayMs(), ReconnectBackoff::delaysMs[1]);
  backoff.onFailure();
  IS_EQUAL(backoff.getDelayMs(), ReconnectBackoff::delaysMs[2]);
  END_IT
}

bool test_the_ladder_saturates_at_the_top() {
  IT("failures past the top rung keep the ceiling instead of wrapping");
  ReconnectBackoff backoff;
  for(uint8_t i = 0U; i < (ReconnectBackoff::stepCount + 5U); i++) {
    backoff.onFailure();
  }
  IS_EQUAL(backoff.getStepIndex(), ReconnectBackoff::stepCount - 1U);
  IS_EQUAL(backoff.getDelayMs(), ReconnectBackoff::delaysMs[ReconnectBackoff::stepCount - 1U]);
  END_IT
}

bool test_success_starts_the_ladder_over() {
  IT("a connection that has proven itself drops the wait back to the first rung");
  ReconnectBackoff backoff;
  backoff.onFailure();
  backoff.onFailure();
  backoff.onSuccess();
  IS_EQUAL(backoff.getStepIndex(), 0U);
  IS_EQUAL(backoff.getDelayMs(), ReconnectBackoff::delaysMs[0]);
  END_IT
}

bool test_a_stored_rung_is_adopted() {
  IT("a rung carried across a reset is picked up where it left off");
  ReconnectBackoff backoff;
  backoff.restore(2U);
  IS_EQUAL(backoff.getStepIndex(), 2U);
  IS_EQUAL(backoff.getDelayMs(), ReconnectBackoff::delaysMs[2]);
  END_IT
}

bool test_a_corrupted_rung_is_clamped_to_the_ceiling() {
  IT("a stored rung past the ladder is clamped instead of reading out of bounds");
  ReconnectBackoff backoff;
  backoff.restore(200U);
  IS_EQUAL(backoff.getStepIndex(), ReconnectBackoff::stepCount - 1U);
  IS_EQUAL(backoff.getDelayMs(), ReconnectBackoff::delaysMs[ReconnectBackoff::stepCount - 1U]);
  END_IT
}

int main() {
  SUITE("ReconnectBackoff");
  test_the_first_attempt_uses_the_first_rung();
  test_each_failure_climbs_one_rung();
  test_the_ladder_saturates_at_the_top();
  test_success_starts_the_ladder_over();
  test_a_stored_rung_is_adopted();
  test_a_corrupted_rung_is_clamped_to_the_ceiling();
  FINISH
}
