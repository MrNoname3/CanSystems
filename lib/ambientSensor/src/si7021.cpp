#include "si7021.hpp"
#include "byteOrder.hpp"                                            /// Two bytes to a 16-bit value.

SI7021::SI7021(uint32_t timeoutUs, uint8_t address, TwoWire& wire) :
  timeoutUs(timeoutUs),
  address(address),
  wire(wire) {}

bool SI7021::init() { // NOLINT(readability-make-member-function-const)
  wire.begin();
  // After begin(): it runs twi_init(), which rewrites TWBR and drops an earlier setClock().
  wire.setClock(clockSpeed);                              // Set I2C bus speed.
  wire.setWireTimeout(timeoutUs, true);                   // Set I2C timeout.
  wire.beginTransmission(address);
  deviceExists = (wire.endTransmission() == 0U);
  return deviceExists;
}

bool SI7021::getCelsiusHundredths(int16_t& temperature) {
  if(!deviceExists) { return false; }
  uint8_t tempBytes[2];
  const uint8_t command = static_cast<uint8_t>(SI7021Commands::TEMP_READ);
  if(!writeReg(&command, sizeof(command)) || !readReg(tempBytes, sizeof(tempBytes))) { return false; }
  const uint32_t tempRaw = be16(tempBytes[0], tempBytes[1]);
  temperature = static_cast<int16_t>(static_cast<int32_t>((17572U * tempRaw) >> 16U) - 4685);
  return true;
}

bool SI7021::getHumidityPercent(uint16_t& humidity) {
  if(!deviceExists) { return false; }
  uint8_t humBytes[2];
  const uint8_t command = static_cast<uint8_t>(SI7021Commands::RH_READ);
  if(!writeReg(&command, sizeof(command)) || !readReg(humBytes, sizeof(humBytes))) { return false; }
  const uint32_t humRaw = be16(humBytes[0], humBytes[1]);
  int32_t humPercent = static_cast<int32_t>((125U * humRaw) >> 16U) - 6;
  if(humPercent < minHumidityPercent) { humPercent = minHumidityPercent; }
  if(humPercent > maxHumidityPercent) { humPercent = maxHumidityPercent; }
  humidity = static_cast<uint16_t>(humPercent);
  return true;
}

bool SI7021::writeReg(const uint8_t* reg, uint8_t regLen) {
  if(reg == nullptr || regLen == 0U) { return false; }
  wire.beginTransmission(address);
  for(uint8_t i = 0U; i < regLen; ++i) {
    wire.write(reg[i]);
  }
  return (wire.endTransmission() == 0U);
}

bool SI7021::readReg(uint8_t* reg, uint8_t regLen) { // NOLINT(readability-non-const-parameter)
  if(reg == nullptr || regLen == 0U) { return false; }
  // Every byte has to arrive: read() answers -1 once the buffer runs dry, which would reach the
  // conversion as 0xFF and come out as a plausible-looking reading instead of an error.
  if(wire.requestFrom(address, regLen) != regLen) { return false; }
  for(uint8_t i = 0U; i < regLen; ++i) {
    reg[i] = static_cast<uint8_t>(wire.read());
  }
  return true;
}

bool SI7021::setPrecision(Precision precision) {
  if(!deviceExists) { return false; }
  uint8_t reg = 0U;
  const uint8_t command = static_cast<uint8_t>(SI7021Commands::USER1_READ);
  if(!writeReg(&command, sizeof(command)) || !readReg(&reg, sizeof(reg))) { return false; }
  reg = static_cast<uint8_t>((reg & 0x7EU) | (static_cast<uint8_t>(precision) & 0x81U));
  const uint8_t userWrite[2] = { static_cast<uint8_t>(SI7021Commands::USER1_WRITE), reg };
  return writeReg(userWrite, sizeof(userWrite));
}

bool SI7021::setHeater(bool on) {
  if(!deviceExists) { return false; }
  uint8_t reg = 0U;
  const uint8_t command = static_cast<uint8_t>(SI7021Commands::USER1_READ);
  if(!writeReg(&command, sizeof(command)) || !readReg(&reg, sizeof(reg))) { return false; }
  reg = static_cast<uint8_t>((reg & ~0x04U) | (on ? 0x04U : 0x00U));
  const uint8_t userWrite[2] = { static_cast<uint8_t>(SI7021Commands::USER1_WRITE), reg };
  return writeReg(userWrite, sizeof(userWrite));
}
