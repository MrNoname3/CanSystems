#include "crc32.hpp"

Crc32::Crc32(uint32_t initValue, uint32_t polynomial) :
  crc_(initValue),
  initValue_(initValue),
  polynomial_(polynomial)
{}

void Crc32::next(uint8_t value) {
  crc_ ^= (uint32_t)value;
  for(uint8_t i = 0; i < 8; i++) {
    if(crc_ & 1) {
      crc_ = (crc_ >> 1) ^ polynomial_;
    }
    else {
      crc_ >>= 1;
    }
  }
}

void Crc32::next(const uint8_t* values, uint32_t length) {
  for (uint32_t i = 0; i < length; i++) {
    next(values[i]);
  }
}

uint32_t Crc32::get() const { return ~crc_; }   // Final CRC32 value is complemented.

void Crc32::reset() { crc_ = initValue_; }

uint32_t Crc32::calculate(const uint8_t *data, uint32_t length) {
  Crc32 crc;
  crc.next(data, length);
  return crc.get();
}