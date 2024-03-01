#ifndef PUSHBUTTONCLICKS_HPP
#define PUSHBUTTONCLICKS_HPP

#include <stdint.h>

/// @brief This class processes button pressing.
class PushButton {
public:
  /// @brief Set parameters for button press processing.
  /// @param deadTime Pause processing after the last event.
  /// @param longPressTime After this time event becomes long press evnet.
  /// @param debounceTime Button press debounce time.
  /// @param buttonPolarity Button pressed polarity.
  PushButton(uint8_t deadTime, uint16_t longPressTime, uint8_t debounceTime, bool buttonPolarity);
  /// @brief Checks for events.
  /// @param currentMillis Time source, like millis() function.
  /// @param currentPinStatus Actual pin status from a digitalRead() function.
  /// @return Returns with the processed event: 1->long press, 2->long release, 3->single tap, 4->double tap...
  uint8_t buttonCheck(const uint32_t currentMillis, bool currentPinStatus);
  PushButton(const PushButton&) = delete;               // Define copy constructor.
  PushButton& operator=(const PushButton&) = delete;    // Define copy assignment operator.
  PushButton(PushButton&&) = delete;                    // Define move constructor.
  PushButton& operator=(PushButton&&) = delete;         // Define move assignment operator.
private:
  const uint8_t deadTime;
  const uint16_t longPressTime;
  const uint8_t debounceTime;
  const bool buttonPolarity;
  uint32_t lastCheckedTime;
  uint32_t pressedDuratoin;
  uint32_t lastEventTime;
  bool longPressflag;
  uint8_t shortPressedCnt;
};
#endif // PUSHBUTTONCLICKS_HPP
