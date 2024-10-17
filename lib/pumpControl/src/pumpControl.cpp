#include "pumpControl.hpp"
#include <Arduino.h>

volatile uint16_t PumpControl::flowCounter = 0U;

PumpControl::PumpControl(PCF8574& pcf8574, uint8_t pwmPin, uint8_t intPin) :
  pcf(pcf8574),
  pwmPin(pwmPin),
  intPin(intPin)
{
  pinMode(pwmPin, OUTPUT);
  pinMode(intPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(intPin), irqHandler, FALLING);
}

void PumpControl::init() {

}

void PumpControl::run() {

}

void PumpControl::irqHandler() {
  flowCounter++;
}

bool PumpControl::selectChannel(CH channel) {
  const uint8_t actualRegValue = pcf.getRegisterValue();
  uint8_t newRegValue = actualRegValue & 0xF0;            // Keep high 4 bits, clear low 4 bits.
  newRegValue |= static_cast<uint8_t>(channel);           // Apply the new channel selection.
  return pcf.write(newRegValue);
}