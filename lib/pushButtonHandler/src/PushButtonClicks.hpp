#ifndef PUSHBUTTONCLICKS_HPP
#define PUSHBUTTONCLICKS_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief This class processes button press events with support for long presses and multiple taps.
/// @details Handles button press event debouncing, timing, and detection of various event types like
/// long presses, single taps, and multiple taps.
class PushButton final {
public:
  /// @brief Constructs a `PushButton` object.
  /// @param deadTime Minimum time (in ms) between events after the last event is processed.
  /// @param longPressTime Time (in ms) after which a button press is considered a long press.
  /// @param debounceTime Time (in ms) used to filter out noise from button presses.
  /// @param buttonPolarity Polarity of the button when pressed (`true` for HIGH, `false` for LOW).
  PushButton(uint8_t deadTime, uint16_t longPressTime, uint8_t debounceTime, bool buttonPolarity);

  /// @brief Destructor of the object.
  ~PushButton() = default;

  /// @brief Checks for button press events.
  /// @details Determines the type of button event based on the timing of button presses and releases.
  /// @param currentMillis Current time in milliseconds, typically provided by a timer like `millis()`.
  /// @param currentPinStatus Current pin status, typically from a `digitalRead()` function.
  /// @return Event code:
  ///   - 1: Long press event (button held for longer than `longPressTime` without release).
  ///   - 2: Long release event (button held for longer than `longPressTime` and released).
  ///   - 3: Single tap event.
  ///   - 4 or higher: Multiple tap event (e.g., 4 = double tap).
  uint8_t buttonCheck(const uint32_t currentMillis, bool currentPinStatus);

  PushButton(const PushButton&) = delete;               // Define copy constructor.
  PushButton& operator=(const PushButton&) = delete;    // Define copy assignment operator.
  PushButton(PushButton&&) = delete;                    // Define move constructor.
  PushButton& operator=(PushButton&&) = delete;         // Define move assignment operator.

private:
  const uint8_t deadTime;                               // Minimum pause time (in ms) after an event is processed.
  const uint16_t longPressTime;                         // Duration (in ms) to classify a press as a long press.
  const uint8_t debounceTime;                           // Debounce duration (in ms) to filter noisy signals.
  const bool buttonPolarity;                            // Polarity of the button when pressed (`true` for HIGH, `false` for LOW).
  uint32_t lastCheckedTime;                             // Timestamp of the last button check.
  uint32_t pressedDuratoin;                             // Duration (in ms) the button has been pressed.
  uint32_t lastEventTime;                               // Timestamp of the last processed event.
  bool longPressflag;                                   // Flag indicating whether a long press event has been triggered.
  uint8_t shortPressedCnt;                              // Counter for short presses (used for multi-tap detection).
};
#endif // PUSHBUTTONCLICKS_HPP
