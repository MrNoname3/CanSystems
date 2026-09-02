#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "common.hpp"                                               /// Time helpers.

/// @brief Growing wait between reconnect attempts.
/// @details Walks a fixed ladder: the first attempt after a drop waits `delaysMs[0]`, every further
/// failure climbs one rung, and the top rung repeats for as long as the outage lasts. To make the
/// device hold off longer before its very first retry, delete leading entries of `delaysMs` - the
/// ladder simply starts further along, no other code changes.
/// @details Kept free of the network stack and of the persistent store so it can be unit-tested on
/// the host: the caller decides when a wait has elapsed and where the rung is kept across a reset.
class ReconnectBackoff final {
public:
  /// @brief Wait before each successive attempt; the last entry is the ceiling and repeats.
  // clang-format off
  static constexpr uint32_t delaysMs[] = {
    Time::secToMs(5U),
    Time::secToMs(10U),
    Time::secToMs(20U),
    Time::secToMs(40U),
    Time::secToMs(80U),
    Time::secToMs(160U),
    Time::secToMs(300U),    // Ceiling: five minutes apart while the broker stays away.
  };
  // clang-format on
  static constexpr uint8_t stepCount = static_cast<uint8_t>(sizeof(delaysMs) / sizeof(delaysMs[0]));
  static_assert(stepCount > 0U, "The backoff ladder needs at least one rung!");

  /// @brief The wait the caller owes before the next attempt.
  [[nodiscard]] uint32_t getDelayMs() const { return delaysMs[stepIndex]; }

  /// @brief The rung in use; persist this to carry the backoff across a reset.
  [[nodiscard]] uint8_t getStepIndex() const { return stepIndex; }

  /// @brief Registers a failed attempt: one rung up, stopping at the top.
  void onFailure() {
    if((stepIndex + 1U) < stepCount) { stepIndex++; }
  }

  /// @brief Registers a connection that has proven itself: back to the first rung.
  void onSuccess() { stepIndex = 0U; }

  /// @brief Adopts a rung carried across a reset.
  /// @param index Value from the persistent store. Anything past the ladder is clamped to the top
  /// rung, so a corrupted record can neither disable the backoff nor stall the device for ever.
  void restore(uint8_t index) {
    stepIndex = (index < stepCount) ? index : static_cast<uint8_t>(stepCount - 1U);
  }

private:
  uint8_t stepIndex = 0U;                                           // Rung of `delaysMs` the next attempt waits out.
};
