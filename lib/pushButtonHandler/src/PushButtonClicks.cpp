#include "PushButtonClicks.hpp"

PushButton::PushButton(uint8_t deadTime, uint16_t longPressTime, uint8_t debounceTime, bool buttonPolarity) :
  deadTime(deadTime),
  longPressTime(longPressTime),
  debounceTime(debounceTime),
  buttonPolarity(buttonPolarity),
  lastCheckedTime(0U),
  pressedDuration(0U),
  lastEventTime(0U),
  longPressflag(false),
  shortPressedCnt(2U) {}

uint8_t PushButton::buttonCheck(const uint32_t currentMillis, bool currentPinStatus) {
  uint8_t output = 0U;
  if(currentPinStatus == buttonPolarity) {
    pressedDuration += currentMillis - lastCheckedTime;
    if(pressedDuration > longPressTime && !longPressflag) {
      output = 1U;        // Long Event without release.
      longPressflag = true;
    }
  } else {
    if(pressedDuration > longPressTime) {
      output = 2U;        // Long Event with release.
      lastEventTime = currentMillis;
      shortPressedCnt = 2U;
    }
    if(pressedDuration > debounceTime && pressedDuration <= longPressTime) {
      shortPressedCnt++;
      lastEventTime = currentMillis;
    }
    if((currentMillis - lastEventTime) > deadTime && shortPressedCnt > 2U) {
      output = shortPressedCnt;
      shortPressedCnt = 2U;
    }
    longPressflag = false;
    pressedDuration = 0U;
  }
  lastCheckedTime = currentMillis;
  return output;
}