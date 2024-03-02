#include "crc16.hpp"

Crc16::Crc16(uint16_t initValue, uint16_t polynomial) :
  crc_(initValue),
  polynomial_(polynomial)
{}

void Crc16::next(uint8_t value) {
  crc_ ^= ((uint16_t)value << 8);
  for(uint8_t i = 0U; i < 8U; i++) {
    if(crc_ & 0x8000) {
      crc_ = (crc_ << 1) ^ polynomial_;
    }
    else {
      crc_ <<= 1;
    }
  }
}

void Crc16::next(const uint8_t* values, uint32_t length) {
  for(uint32_t i = 0; i < length; i++) {
    next(values[i]);
  }
}

uint16_t Crc16::get() const { return crc_; }

uint16_t Crc16::calculate(const uint8_t *data, uint32_t length) {
  Crc16 crc;
  crc.next(data, length);
  return crc.get();
}