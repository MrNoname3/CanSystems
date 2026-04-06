#include "multiplexer.hpp"
#include <Arduino.h>

Multiplexer::Multiplexer(uint8_t readPin, uint8_t enablePin, const uint8_t (&chSelectPins)[numSelectPins]) :
  readPin(readPin),
  enablePin(enablePin),
  chSelectPins(chSelectPins)
{
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH);
  for(uint8_t i = 0U; i < numSelectPins; ++i) {
    pinMode(chSelectPins[i], OUTPUT);
  }
}

void Multiplexer::selectChannel(uint8_t channel) const { // NOLINT(readability-convert-member-functions-to-static)
  channel &= 15U; // Ensure only the lower 4 bits are used.
  for(uint8_t i = 0U; i < numSelectPins; ++i) {
    // Set each pin HIGH or LOW based on the respective bit in 'channel'.
    digitalWrite(chSelectPins[i], (channel & (1U << i)) != 0U ? HIGH : LOW);
  }
}

void Multiplexer::enableRead() const {
  digitalWrite(enablePin, LOW);
}

void Multiplexer::disableRead() const {
  digitalWrite(enablePin, HIGH);
}

uint16_t Multiplexer::analogReadSimple(uint8_t channel) const {
  selectChannel(channel);
  enableRead();
  const uint16_t adcValue = analogRead(readPin);
  disableRead();
  return adcValue;
}

uint16_t Multiplexer::analogReadAdvanced(uint8_t channel) const {
  selectChannel(channel);
  return analogRead(readPin);
}

uint16_t Multiplexer::analogReadAdvanced() const {
  return analogRead(readPin);
}