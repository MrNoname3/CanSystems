#include "pcf8574.hpp"

PCF8574::PCF8574(uint8_t address, TwoWire &wire) :
  address(address),
  wire(wire),
  registerValue(0xFFU)
{}

bool PCF8574::begin() {
  wire.begin();
  wire.setClock(clockSpeed);
  wire.beginTransmission(address);
  return (wire.endTransmission() == 0U);
}

bool PCF8574::write(uint8_t reg) {
  wire.beginTransmission(address);
  wire.write(reg);
  const bool result = wire.endTransmission() == 0U;
  if(result) { registerValue = reg; }
  return result;
}

bool PCF8574::read(uint8_t &value) {
  const bool result = (wire.requestFrom(address, 1U) > 0U);
  if(result) { value = wire.read(); }
  return result;
}

uint8_t PCF8574::getRegisterValue() {
  return registerValue;
}

bool PCF8574::setAsInput(Pin pin) {
  return digitalWrite(pin, PinState::H);                  // Input mode is just a HIGH state (high state is always pull-up).
}

bool PCF8574::digitalWrite(Pin pin, PinState pinState) {
  const uint8_t mask = 1U << static_cast<uint8_t>(pin);   // Bit mask for the pin.
  if (pinState != PinState::L) {
    registerValue |= mask;                                // Set pin high.
  } else {
    registerValue &= ~mask;                               // Set pin low.
  }
  return write(registerValue);
}

PCF8574::PinState PCF8574::digitalRead(Pin pin) {
  uint8_t value = 0;
  const bool result = read(value);
  if(!result) { return PinState::E; }
  const bool state = (value & (1U << static_cast<uint8_t>(pin))) == 0;
  return state ? PinState::H : PinState::L;
}

bool PCF8574::toggleState(Pin pin) {
  const uint8_t mask = 1U << static_cast<uint8_t>(pin);   // Bit mask for the pin.
  return write(registerValue ^ mask);                     // Toggle the pin by XORing the bit.
}
