#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Moves pending CAN frames across one scheduler pass, in either direction.
/// @details Both ends of a CAN handler hold more than one frame: a controller has several
/// receive buffers, and an outgoing queue fills faster than a task sending one frame per pass
/// can empty it. The pump moves frames until the source reports empty, bounded by a frame
/// budget so a burst cannot starve the other tasks in the cooperative loop.
///
/// Kept free of the CAN driver and the AVR plumbing so it can be unit-tested on the host: the
/// two operations are supplied as callables.
namespace CanFramePump {
  /// @brief Outcome of one drain pass.
  struct Result {
    uint8_t handled = 0U;         // Frames taken from the source and handled.
    bool failed = false;          // A frame could not be handled; the pass stopped on it.
  };

  /// @brief Takes and handles pending frames until the source is empty or the budget runs out.
  /// @tparam TakeFrame Callable returning `true` when it took a frame, `false` when none was pending.
  /// @tparam HandleFrame Callable returning `false` when the frame it processed was rejected.
  /// @param takeFrame Fetches the next frame from the source.
  /// @param handleFrame Processes the frame `takeFrame` just fetched.
  /// @param maxFrames Frame budget for this pass; 0 takes nothing.
  /// @return How many frames were handled, and whether the pass stopped on a rejected one.
  template<typename TakeFrame, typename HandleFrame>
  [[nodiscard]] inline Result drain(TakeFrame takeFrame, HandleFrame handleFrame, uint8_t maxFrames) {
    Result result;
    while((result.handled < maxFrames) && takeFrame()) {
      if(!handleFrame()) {
        result.failed = true;
        break;
      }
      ++result.handled;
    }
    return result;
  }
} // namespace CanFramePump
