#include "si7021.hpp"

SI7021::SI7021(uint32_t timeoutUs, uint8_t address, TwoWire &wire) :
  address(address),
  wire(wire),
  deviceExists(false)
{
  this->wire.setClock(clockSpeed);                        // Set I2C bus speed.
  this->wire.setWireTimeout(timeoutUs, true);             // Set I2C timeout.
}

bool SI7021::init() {
  wire.begin();
  wire.beginTransmission(address);
  deviceExists = (wire.endTransmission() == 0U);
  return deviceExists;
}

bool SI7021::getCelsiusHundredths(int16_t &temperature) {
  if(!deviceExists) { return false; }
  uint8_t tempBytes[2];
  const uint8_t command = static_cast<const uint8_t>(SI7021Commands::TEMP_READ);
  if(!writeReg(&command, sizeof(command)) || !readReg(tempBytes, sizeof(tempBytes))) { return false; }
  int32_t tempRaw = (int32_t)tempBytes[0] << 8U | tempBytes[1];
  temperature = static_cast<int16_t>((((17572 * tempRaw) >> 16U) - 4685));
  return true;
}

bool SI7021::getHumidityPercent(uint16_t &humidity) {
  if(!deviceExists) { return false; }
  uint8_t humBytes[2];
  const uint8_t command = static_cast<const uint8_t>(SI7021Commands::RH_READ);
  if(!writeReg(&command, sizeof(command)) || !readReg(humBytes, sizeof(humBytes))) { return false; }
  int32_t humRaw = (int32_t)humBytes[0] << 8U | humBytes[1];
  humidity = ((125 * humRaw) >> 16U) - 6;
  return true;
}

bool SI7021::writeReg(const uint8_t *reg, uint8_t regLen) {
  if(reg == nullptr || regLen == 0U) { return false; }
  wire.beginTransmission(address);
  for(uint8_t i = 0U; i < regLen; ++i) {
    wire.write(reg[i]);
  }
  return (wire.endTransmission() == 0U);
}

bool SI7021::readReg(uint8_t *reg, uint8_t regLen) {
  if(reg == nullptr || regLen == 0U) { return false; }
  const bool result = (wire.requestFrom(address, regLen) > 0U);
  if(result) {
    for(uint8_t i = 0U; i < regLen; ++i) {
      reg[i] = wire.read();
    }
  }
  return result;
}

bool SI7021::setPrecision(Precision precision) {
  if(!deviceExists) { return false; }
  uint8_t reg = 0U;
  const uint8_t command = static_cast<const uint8_t>(SI7021Commands::USER1_READ);
  if(!writeReg(&command, sizeof(command)) || !readReg(&reg, sizeof(reg))) { return false; }
  reg = (reg & 0x7E) | (static_cast<uint8_t>(precision) & 0x81);
  const uint8_t userWrite[2] = {static_cast<const uint8_t>(SI7021Commands::USER1_WRITE), reg};
  return writeReg(userWrite, sizeof(userWrite));
}

bool SI7021::setHeater(bool on) {
  if(!deviceExists) { return false; }
  uint8_t reg = 0U;
  const uint8_t command = static_cast<const uint8_t>(SI7021Commands::USER1_READ);
  if (!writeReg(&command, sizeof(command)) || !readReg(&reg, sizeof(reg))) { return false; }
  reg = (reg & ~0x04) | (on ? 0x04 : 0x00);
  const uint8_t userWrite[2] = {static_cast<const uint8_t>(SI7021Commands::USER1_WRITE), reg};
  return writeReg(userWrite, sizeof(userWrite));
}