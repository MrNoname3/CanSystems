#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief A handful of bytes that survive a reset but not a power cycle.
/// @details That lifetime is what the users are after: a reset - watchdog, restartMCU() or
/// otherwise - keeps what the previous run left, while pulling the plug is a deliberate clean
/// slate. Where the bytes live is platform business: RTC user memory on the ESP8266,
/// RTC_NOINIT_ATTR variables on the ESP32, plain statics on the host so everything above this can
/// be unit-tested.
/// @details Each slot carries its own written marker, so a slot no earlier run has written still
/// reads back as absent after another slot has been written in this one - which is what keeps a
/// power-on start from mistaking a neighbour's fresh value for its own history.
class RtcStore final {
public:
  /// @brief The values kept across a reset. Slots are independent of one another.
  enum class Slot : uint8_t {
    BackoffStep = 0U,                                               // Rung of the reconnect backoff ladder.
    BootStage = 1U,                                                 // How far the startup sequence got.
  };

  /// @brief How many slots the record holds.
  static constexpr uint8_t slotCount = 2U;

  /// @brief Reads a slot.
  /// @param slot Which value to read.
  /// @param value Receives the value when this returns `true`; untouched otherwise.
  /// @return `false` when no run has written that slot, which is the case after a power cycle.
  [[nodiscard]] static bool read(Slot slot, uint8_t& value);

  /// @brief Writes a slot, leaving every other one as it was.
  /// @param slot Which value to write.
  /// @param value The value to keep.
  static void write(Slot slot, uint8_t value);

  RtcStore() = delete;                                              // Delete constructor.
  ~RtcStore() = delete;                                             // Delete destructor.
  RtcStore(const RtcStore&) = delete;                               // Delete copy constructor.
  RtcStore& operator=(const RtcStore&) = delete;                    // Delete copy assignment operator.
  RtcStore(RtcStore&&) = delete;                                    // Delete move constructor.
  RtcStore& operator=(RtcStore&&) = delete;                         // Delete move assignment operator.
};
