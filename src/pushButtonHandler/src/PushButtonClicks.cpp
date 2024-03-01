#include "PushButtonClicks.hpp"

PushButton::PushButton(uint8_t deadTime, uint16_t longPressTime, uint8_t debounceTime, bool buttonPolarity) :
  deadTime(deadTime),
  longPressTime(longPressTime),
  debounceTime(debounceTime),
  buttonPolarity(buttonPolarity),
  lastCheckedTime(0U),
  pressedDuratoin(0U),
  lastEventTime(0U),
  longPressflag(0U),
  shortPressedCnt(2U)
{}

uint8_t PushButton::buttonCheck(const uint32_t currentMillis, bool currentPinStatus) {
  uint8_t output = 0U;
  if(currentPinStatus == buttonPolarity) {
    pressedDuratoin += currentMillis - lastCheckedTime;
    if(pressedDuratoin > longPressTime && longPressflag == 0U) {
      output = 1U;        // Long Event without release.
      longPressflag = 1U;
    }
  }
  else {
    if(pressedDuratoin > longPressTime) {
      output = 2U;        // Long Event with release.
      lastEventTime = currentMillis;
      shortPressedCnt = 2U;
    }
    if(pressedDuratoin > debounceTime && pressedDuratoin <= longPressTime) {
      shortPressedCnt++;
      lastEventTime = currentMillis;
    }
    if(currentMillis > lastEventTime + deadTime && shortPressedCnt > 2U) {
      output = shortPressedCnt;
      shortPressedCnt = 2U;
    }
    longPressflag = 0U;
    pressedDuratoin = 0U;
  }
  lastCheckedTime = currentMillis;
  return output;
}