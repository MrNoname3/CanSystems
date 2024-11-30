#include "pcf8574.hpp"

PCF8574::PCF8574(uint32_t timeoutUs, uint8_t address, TwoWire &wire) :
  address(address),
  wire(wire),
  registerValue(0xFFU),
  deviceExists(false)
{
  this->wire.setClock(clockSpeed);
  this->wire.setWireTimeout(timeoutUs, true);
}

bool PCF8574::init() {
  wire.begin();
  wire.beginTransmission(address);
  deviceExists = (wire.endTransmission() == 0U);
  return deviceExists;
}

bool PCF8574::write(uint8_t reg) {
  if(!deviceExists) { return false; }
  wire.beginTransmission(address);
  wire.write(reg);
  const bool result = (wire.endTransmission() == 0U);
  if(result) { registerValue = reg; }
  return result;
}

bool PCF8574::read(uint8_t &value) const {
  if(!deviceExists) { return false; }
  const bool result = (wire.requestFrom(address, 1U) > 0U);
  if(result) { value = wire.read(); }
  return result;
}

const uint8_t PCF8574::getRegisterValue() const {
  return static_cast<const uint8_t>(registerValue);
}

bool PCF8574::setAsInput(uint8_t pin) {
  return digitalWrite(pin, 1U);                           // Input mode is just a HIGH state (high state is always pull-up).
}

bool PCF8574::digitalWrite(uint8_t pin, uint8_t pinState) {
  if(pin > 7U) { return false; }                          // Validate pin range (0-7).
  const uint8_t mask = 1U << pin;                         // Bit mask directly using pin (0-7).
  if (pinState > 0U) {
    registerValue |= mask;                                // Set pin high.
  } else {
    registerValue &= ~mask;                               // Set pin low.
  }
  return write(registerValue);
}

uint8_t PCF8574::digitalRead(uint8_t pin) const {
  uint8_t value = 0U;
  if(!read(value) || pin > 7U) { return -1; }
  return (value & (1U << static_cast<uint8_t>(pin))) > 0U ? 1U : 0U;
}

bool PCF8574::toggleState(uint8_t pin) {
  if(pin > 7U) { return false; }                          // Validate pin range (0-7).
  const uint8_t mask = 1U << pin;                         // Bit mask directly using pin (0-7).
  registerValue ^= mask;                                  // Toggle the pin by XORing the bit.
  return write(registerValue);
}
