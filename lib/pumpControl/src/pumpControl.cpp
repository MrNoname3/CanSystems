#include "pumpControl.hpp"
#include <Arduino.h>

volatile uint16_t PumpControl::flowCounter = 0U;

PumpControl::PumpControl(const PCF8574& pcf8574, uint8_t pwmPin, uint8_t intPin) :
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