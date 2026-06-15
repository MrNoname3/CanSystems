#include "pushButtonHandler.hpp"
#include <Arduino.h>

PushButtonHandler::PushButtonHandler(const CanHandler& canHandler, bool (*buttonReader)()) :
  canHandler(canHandler),
  readButtonValue(buttonReader),
  button(deadTime, longPressTime, debounceTime, buttonPolarity),
  btnCallback(nullptr) {}

bool PushButtonHandler::run() {
  const uint8_t event = readButtonValue == nullptr ? 0U : button.buttonCheck(millis(), readButtonValue());
  const bool eventValid = event > 0U;
  if(eventValid) {
    const uint8_t canData[8] = { event, 0U, 0U, 0U, 0U, 0U, 0U, 0U };
    canHandler.send(CanCmd::BUTTON_EVENT, canData);
    if((btnCallback != nullptr) && (event < static_cast<uint8_t>(BtnEvent::LAST_ELEMENT))) {
      btnCallback(static_cast<BtnEvent>(event));
    }
  }
  return true;
}

void PushButtonHandler::addBtnCallback(void (*btnCallback)(BtnEvent btnEvent)) {
  this->btnCallback = btnCallback;
}
