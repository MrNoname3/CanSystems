#include "crc8.hpp"

Crc8::Crc8(uint8_t initValue, uint8_t polynomial, bool refIn, bool refOut, uint8_t xorOut) :
  crc_(initValue),
  initValue_(initValue),
  polynomial_(polynomial),
  refIn_(refIn),
  refOut_(refOut),
  xorOut_(xorOut)
{}

uint8_t Crc8::reflect(uint8_t value) {
  uint8_t reflected = 0;
  for(uint8_t i = 0; i < 8; ++i) {
    if(value & (1 << i)) {
      reflected |= (1 << (7 - i));
    }
  }
  return reflected;
}

void Crc8::next(uint8_t value) {
  if(refIn_) {
    value = reflect(value);
  }
  
  crc_ ^= value;
  for(uint8_t i = 0U; i < 8U; i++) {
    if(crc_ & 0x80U) {
      crc_ = (crc_ << 1U) ^ polynomial_;
    } else {
      crc_ <<= 1U;
    }
  }
}

void Crc8::next(const uint8_t* values, uint32_t length) {
  if(values == nullptr || length == 0UL) return;
  for(uint32_t i = 0UL; i < length; i++) {
    next(values[i]);
  }
}

uint8_t Crc8::get() const { 
  uint8_t result = crc_;
  if(refOut_) {
    result = reflect(result);
  }
  return result ^ xorOut_;
}

void Crc8::reset() { crc_ = initValue_; }

uint8_t Crc8::calculate(const uint8_t *data, uint32_t length, uint8_t initValue, uint8_t polynomial,
                        bool refIn, bool refOut, uint8_t xorOut) {
  if(data == nullptr || length == 0UL) return initValue ^ xorOut;
  Crc8 crc(initValue, polynomial, refIn, refOut, xorOut);
  crc.next(data, length);
  return crc.get();
}

bool Crc8::verify(const uint8_t* data, uint32_t length, uint8_t expected,
                  uint8_t initValue, uint8_t polynomial, bool refIn, bool refOut, uint8_t xorOut) {
  if(data == nullptr || length == 0UL) return expected == (initValue ^ xorOut);
  return calculate(data, length, initValue, polynomial, refIn, refOut, xorOut) == expected;
}