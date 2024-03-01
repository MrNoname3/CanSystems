#include "pushButtonHandler.hpp"
#include <Arduino.h>

PushButtonHandler::PushButtonHandler(HardwareSerial& serial, const CanHandler& canHandler, const uint8_t buttonPin) :
  serialPort(serial),
  canHandler(canHandler),
  buttonPin(buttonPin),
  button(deadTime, longPressTime, debounceTime, buttonPolarity),
  btnCallback(nullptr)
{
}

void PushButtonHandler::loop() {
  const uint8_t event = button.buttonCheck(millis(), analogRead(buttonPin) > 511 ? HIGH : LOW);
  const bool eventValid = event > 0;
  if(eventValid) {
    serialPort.print(F("Btn: "));
    serialPort.println(event);
    {
      const uint8_t canData[8] = { event, 0, 0, 0, 0, 0, 0, 0 };
      canHandler.send(CanCmd::BUTTON_EVENT, canData);
    }
    if(btnCallback != nullptr) { btnCallback(event); }
  }
}

void PushButtonHandler::addBtnCallback(void (*btnCallback)(const uint8_t& btnEvent)) {
  this->btnCallback = btnCallback;
}