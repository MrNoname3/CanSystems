#ifndef EEPROM_HANDLER_HPP
#define EEPROM_HANDLER_HPP

#include <Arduino.h>                          /// Arduino libraries header.
#include <EEPROM.h>                           /// EEPROM access library.

template<class T, uint16_t eepromAddress>
class EEPROMHandler {

public:

  EEPROMHandler() = default;

  EEPROMHandler(T* data) : data(data) { }

  virtual ~EEPROMHandler() = default;

  bool save() {
    if(data == nullptr) { return false; }
    return save(data);
  }

  static bool save(T* data) {
    EEPROMData eepromData;
    eepromData.data = *data;
    eepromData.crc = calCrc(reinterpret_cast<uint8_t*>(&eepromData), sizeof(EEPROMData));

    // Perform EEPROM write operation
    EEPROM.put(eepromAddress, eepromData);

    // Verify the data written to EEPROM
    for (uint16_t i = 0; i < sizeof(EEPROMData); ++i) {
      if (EEPROM.read(eepromAddress + i) != reinterpret_cast<uint8_t*>(&eepromData)[i]) {
        return false; // Write failed
      }
    }
    return true; // Write successful
  }

  bool load() {
    if(data == nullptr) { return false; }
    return load(data);
  }

  static bool load(T* data) {
    EEPROMData eepromData;
    EEPROM.get(eepromAddress, eepromData);

    uint16_t crcReceived = eepromData.crc;
    eepromData.crc = 0;
    uint16_t crcCalculated = calCrc(reinterpret_cast<uint8_t*>(&eepromData), sizeof(EEPROMData));

    if(crcReceived == crcCalculated) {
      *data = eepromData.data;
      return true;
    }
    else {
      return false;
    }
  }

  static constexpr uint16_t getDataSize() {
    return sizeof(EEPROMData);
  }

  /// @brief Calculates the 16bit CRC (XModem) of the given data.
  /// @param data Data whose CRC value should be calcilated.
  /// @param size Given data size in bytes.
  /// @return Returns with the calculated CRC value.
  static uint16_t calCrc(uint8_t* data, uint16_t length) {
    uint16_t crc = 0x0000;
    for (uint16_t i = 0; i < length; i++) {
      crc ^= ((uint16_t)data[i] << 8);
      for (uint8_t j = 0; j < 8; j++) {
        if (crc & 0x8000) {
          crc = (crc << 1) ^ 0x1021;
        } else {
          crc <<= 1;
        }
      }
    }
    return crc;
  }

  EEPROMHandler(const EEPROMHandler&) = delete;               // Define copy constructor.
  EEPROMHandler& operator=(const EEPROMHandler&) = delete;    // Define copy assignment operator.
  EEPROMHandler(EEPROMHandler&&) = delete;                    // Define move constructor.
  EEPROMHandler& operator=(EEPROMHandler&&) = delete;         // Define move assignment operator.

private:

  T* data = nullptr;

  struct __attribute__((packed)) 
  EEPROMData {    
    uint16_t crc = 0;                           // Data struct CRC value.
    T data;
  };

};


#endif // EEPROM_HANDLER_HPP