#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Reports how much a free-running counter grew since it was last looked at.
/// @details Pairs with a counter an interrupt owns: the interrupt only ever increments, and a
/// task reads it. Keeping the counter free-running - never reset by the reader - means the
/// interrupt stays the only writer, so no increment can be lost to a concurrent clear. The
/// reader holds the mark instead, and unsigned subtraction keeps the growth correct across the
/// counter's wrap.
class DeltaCounter final {
public:
  /// @brief Reads the growth since the previous call and moves the mark up to `current`.
  /// @param current Present value of the counter being watched.
  /// @return How much it grew since the previous call; 0 when it has not moved.
  [[nodiscard]] uint32_t takeGrowth(uint32_t current) {
    const uint32_t growth = current - lastSeen;
    lastSeen = current;
    return growth;
  }

private:
  uint32_t lastSeen = 0U;                                           // Counter value at the previous look.
};
