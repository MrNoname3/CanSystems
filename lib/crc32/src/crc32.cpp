#include "crc32.hpp"

Crc32::Crc32(uint32_t initValue, uint32_t polynomial) :
  crc_(initValue),
  initValue_(initValue),
  polynomial_(polynomial)
{}

void Crc32::next(uint8_t value) {
  crc_ ^= static_cast<uint32_t>(value);         // XOR the input byte into the CRC.
  for(uint8_t i = 0U; i < 8U; i++) {
    if((crc_ & 1U) != 0U) {                     // Check the least significant bit.
      crc_ = (crc_ >> 1U) ^ polynomial_;        // XOR with the polynomial if bit is set.
    } else {
      crc_ >>= 1U;                              // Otherwise, simply shift right.
    }
  }
}

void Crc32::next(const uint8_t* values, uint32_t length) {
  if(values == nullptr || length == 0U) { return; }
  for(uint32_t i = 0U; i < length; i++) {
    next(values[i]);
  }
}

uint32_t Crc32::get() const { return ~crc_; }   // Final CRC32 value is complemented.

void Crc32::reset() { crc_ = initValue_; }

uint32_t Crc32::calculate(const uint8_t* data, uint32_t length, uint32_t initValue, uint32_t polynomial) {
  if(data == nullptr || length == 0U) { return initValue; }
  Crc32 crc(initValue, polynomial);
  crc.next(data, length);
  return crc.get();
}

bool Crc32::verify(const uint8_t* data, uint32_t length, uint32_t expected,
  uint32_t initValue, uint32_t polynomial) {
  if(data == nullptr || length == 0U) { return expected == initValue; }
  return calculate(data, length, initValue, polynomial) == expected;
}