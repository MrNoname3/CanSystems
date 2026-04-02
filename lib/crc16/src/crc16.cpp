#include "crc16.hpp"

Crc16::Crc16(uint16_t initValue, uint16_t polynomial) :
  crc_(initValue),
  initValue_(initValue),
  polynomial_(polynomial)
{}

void Crc16::next(uint8_t value) {
  crc_ ^= static_cast<uint16_t>(value) << 8U;
  for(uint8_t i = 0U; i < 8U; i++) {
    if((crc_ & 0x8000U) != 0U) {
      crc_ = (crc_ << 1U) ^ polynomial_;
    } else {
      crc_ <<= 1U;
    }
  }
}

void Crc16::next(const uint8_t* values, uint32_t length) {
  if(values == nullptr || length == 0UL) { return; }
  for(uint32_t i = 0UL; i < length; i++) {
    next(values[i]);
  }
}

uint16_t Crc16::get() const { return crc_; }

void Crc16::reset() { crc_ = initValue_; }

uint16_t Crc16::calculate(const uint8_t *data, uint32_t length, uint16_t initValue, uint16_t polynomial) {
  if(data == nullptr || length == 0UL) { return initValue; }
  Crc16 crc(initValue, polynomial);
  crc.next(data, length);
  return crc.get();
}

bool Crc16::verify(const uint8_t* data, uint32_t length, uint16_t expected,
  uint16_t initValue, uint16_t polynomial) {
  if(data == nullptr || length == 0UL) { return expected == initValue; }
  return calculate(data, length, initValue, polynomial) == expected;
}