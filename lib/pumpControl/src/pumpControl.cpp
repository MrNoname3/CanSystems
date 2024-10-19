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

void PumpControl::createIrrigation(uint8_t irrigationInfo, uint8_t pwmValue, uint8_t repeatNum) {
  if(!irrigationQueue.isFull()) {
    irrigationQueue.put(IrrigationQueueElement(irrigationInfo, pwmValue, repeatNum));
  }
}

void PumpControl::createIrrigation(uint8_t channel, uint8_t duration, uint8_t pwmValue, uint8_t repeatNum, bool checkFlow) {
  if(!irrigationQueue.isFull()) {
    irrigationQueue.put(IrrigationQueueElement(channel, duration, checkFlow, pwmValue, repeatNum));
  }
}

void PumpControl::irqHandler() {
  flowCounter++;
}

bool PumpControl::selectChannel(uint8_t channel) const {
  const uint8_t actualRegValue = pcf.getRegisterValue();
  uint8_t newRegValue = actualRegValue & 0xF0;            // Keep high 4 bits, clear low 4 bits.
  newRegValue |= (1U << channel);                         // Apply the new channel selection.
  return pcf.write(newRegValue);
}