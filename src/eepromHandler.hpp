#ifndef EEPROM_HANDLER_HPP
#define EEPROM_HANDLER_HPP

#include <Arduino.h>                          /// Arduino libraries header.
#include <EEPROM.h>                           /// EEPROM access library.

/// @brief This class is a wrapper for EEPROM storing and reading.
/// @tparam T Datatype which will be stored.
/// @tparam eepromAddress This is the EEPROM address where we want to store our data. 
template<class T, uint16_t eepromAddress>
class EEPROMHandler {

public:

  /// @brief Default constructor.
  EEPROMHandler() = default;

  /// @brief Constructor with data pointer.
  /// @param data The pointer of the data, which we want to 'save to' / 'load from' EEPROM.
  EEPROMHandler(T* data) : data(data) { }

  /// @brief Default destructor.
  virtual ~EEPROMHandler() = default;

  /// @brief Saves the data pointed to by the pointer in the constructor.
  /// @return Returns the result of the execution.
  bool save() {
    if(data == nullptr) { return false; }               // Nullpointer check.
    return save(data);
  }

  /// @brief Saves the data pointed to by the specified pointer.
  /// @param data Pointer to the data.
  /// @return Returns the result of the execution.
  static bool save(T* data) {
    EEPROMData eepromData;
    eepromData.data = *data;
    eepromData.crc = calCrc(reinterpret_cast<uint8_t*>(&eepromData), sizeof(EEPROMData));

    // Perform EEPROM write operation.
    EEPROM.put(eepromAddress, eepromData);

    // Verify the data written to EEPROM.
    for (uint16_t i = 0; i < sizeof(EEPROMData); ++i) {
      if (EEPROM.read(eepromAddress + i) != reinterpret_cast<uint8_t*>(&eepromData)[i]) {
        return false;     // Write failed.
      }
    }
    return true;          // Write successful.
  }

  /// @brief Loads data into the memory address stored by the pointer specified in the constructor.
  /// @return Returns the result of the execution. 
  bool load() {
    if(data == nullptr) { return false; }               // Nullpointer check.
    return load(data);
  }

  /// @brief Loads data into the memory address stored by the pointer specified here.
  /// @param data Pointer to the data.
  /// @return Returns the result of the execution. 
  static bool load(T* data) {

    // Read data.
    EEPROMData eepromData;
    EEPROM.get(eepromAddress, eepromData);

    // Check CRC.
    uint16_t crcReceived = eepromData.crc;
    eepromData.crc = 0;
    uint16_t crcCalculated = calCrc(reinterpret_cast<uint8_t*>(&eepromData), sizeof(EEPROMData));

    if(crcReceived == crcCalculated) {
      *data = eepromData.data;
      return true;        // CRC OK.
    }
    else {
      return false;       // CRC not OK.
    }
  }

  /// @brief Size of the stored frame.
  /// @return Returns the data frame size which is stored in EEPROM.
  static constexpr uint16_t getDataSize() {
    return sizeof(EEPROMData);
  }

  /// @brief Calculates the 16bit CRC (XModem) of the given data.
  /// @param data Data whose CRC value should be calculated.
  /// @param size Given data size in bytes.
  /// @return Returns with the calculated CRC value.
  static uint16_t calCrc(uint8_t* data, uint16_t size) {
    uint16_t crc = 0x0000;
    for (uint16_t i = 0; i < size; i++) {
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

  T* data;                                      // Pointer of user datatype.

  struct __attribute__((packed)) 
  EEPROMData {                                  // Data frame to stored in EEPROM.
    uint16_t crc;                               // Data struct CRC value.
    T data;                                     // Data type template.
    EEPROMData() : crc(0) { }
  };

};


#endif // EEPROM_HANDLER_HPP