#ifndef CRC32_HPP
#define CRC32_HPP

#include <stdint.h>

class Crc32 {
  static constexpr uint32_t polynomial = 0xEDB88320; // CRC32 polynomial

public:
  Crc32() : crc(0xFFFFFFFF) {} // Initialize CRC32 value

  void next(uint8_t value) {
    crc ^= (uint32_t)value;
    for(uint8_t i = 0; i < 8; i++) {
      if(crc & 1) {
        crc = (crc >> 1) ^ polynomial;
      }
      else {
        crc >>= 1;
      }
    }
  }

  void next(const uint8_t* values, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
      next(values[i]);
    }
  }

  uint32_t get() const { return ~crc; } // Final CRC32 value is complemented

  static uint32_t calculate(const uint8_t *data, uint16_t length) {
    Crc32 crc;
    crc.next(data, length);
    return crc.get();
  }

private:
  uint32_t crc;
};

#endif // CRC32_HPP
