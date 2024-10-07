#include "pushButtonHandler.hpp"
#include <Arduino.h>

PushButtonHandler::PushButtonHandler(HardwareSerial& serial, const CanHandler& canHandler, bool (*buttonReader)()) :
  serialPort(serial),
  canHandler(canHandler),
  readButtonValue(buttonReader),
  button(deadTime, longPressTime, debounceTime, buttonPolarity),
  btnCallback(nullptr)
{
}

void PushButtonHandler::loop() {
  const uint8_t event = readButtonValue == nullptr ? 0U : button.buttonCheck(millis(), readButtonValue());
  const bool eventValid = event > 0U;
  if(eventValid) {
    serialPort.print(F("Btn: "));
    serialPort.println(event);
    {
      const uint8_t canData[8] = { event, 0, 0, 0, 0, 0, 0, 0 };
      canHandler.send(CanCmd::BUTTON_EVENT, canData);
    }
    if(event > static_cast<uint8_t>(BtnEvent::LAST_ELEMENT)) { return; }
    if(btnCallback != nullptr) { btnCallback(static_cast<BtnEvent>(event)); }
  }
}

void PushButtonHandler::addBtnCallback(void (*btnCallback)(BtnEvent btnEvent)) {
  this->btnCallback = btnCallback;
}