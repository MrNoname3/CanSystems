#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Drains a CAN controller's receive buffers within one scheduler pass.
/// @details A controller holds more than one received frame (the MCP2515 has two receive
/// buffers) and keeps its interrupt line asserted while any of them is full. Taking a single
/// frame per pass therefore leaves the line low, which produces no further edge for an
/// edge-triggered handler; the remaining frame stays unread until new traffic arrives.
/// The pump reads until the controller reports empty, bounded by a frame budget so a burst
/// cannot starve the other tasks in the cooperative loop.
///
/// Kept free of the CAN driver and the AVR plumbing so it can be unit-tested on the host: the
/// two operations are supplied as callables, mirroring how OtaCanResponse isolates the OTA
/// handshake decision from canHandlerAtmega328P.
namespace CanRxPump {
  /// @brief Outcome of one drain pass.
  struct Result {
    uint8_t handled = 0U;         // Frames taken from the controller and handled.
    bool failed = false;          // A frame could not be handled; the pass stopped on it.
  };

  /// @brief Reads and handles pending frames until the controller is empty or the budget runs out.
  /// @tparam TakeFrame Callable returning `true` when it took a frame, `false` when none was pending.
  /// @tparam HandleFrame Callable returning `false` when the frame it processed was rejected.
  /// @param takeFrame Fetches the next frame from the controller.
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
} // namespace CanRxPump
