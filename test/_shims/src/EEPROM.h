#pragma once
#include <stdint.h>
#include <string.h>

static constexpr uint16_t EEPROM_SHIM_SIZE = 512U;

class EEPROMClass {
public:
  EEPROMClass() { memset(storage, 0xFFU, sizeof(storage)); }

  template<typename T>
  void put(uint16_t addr, const T& val) {
    if (static_cast<uint32_t>(addr) + sizeof(T) <= EEPROM_SHIM_SIZE) {
      memcpy(storage + addr, &val, sizeof(T));
    }
  }

  template<typename T>
  void get(uint16_t addr, T& val) const {
    if (static_cast<uint32_t>(addr) + sizeof(T) <= EEPROM_SHIM_SIZE) {
      memcpy(&val, storage + addr, sizeof(T));
    }
  }

  void clear() { memset(storage, 0xFFU, sizeof(storage)); }

private:
  uint8_t storage[EEPROM_SHIM_SIZE];
};

inline EEPROMClass EEPROM;
