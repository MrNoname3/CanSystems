#ifndef EEPROM_HANDLER_HPP
#define EEPROM_HANDLER_HPP

#include <stdint.h>
#include <EEPROM.h>                                                 /// EEPROM access library.
#include "../../crc16/src/crc16.hpp"

/// @brief This class is a wrapper for EEPROM storing and reading.
/// @tparam T Datatype which will be stored.
/// @tparam eepromAddress This is the EEPROM address where we want to store our data. 
template<class T, uint16_t eepromAddress>
class EEPROMHandler final {
public:
  /// @brief Constructor of EEPROM handler class.
  EEPROMHandler() : data() {}

  /// @brief Constructor of EEPROM handler class with pointer to the data.
  /// @param data The pointer of the data, which we want to 'save to' / 'load from' EEPROM.
  EEPROMHandler(T* data) : data(data) {}

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
    eepromData.crc = Crc16::calculate(reinterpret_cast<uint8_t*>(&eepromData), sizeof(EEPROMData));

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
    uint16_t crcCalculated = Crc16::calculate(reinterpret_cast<uint8_t*>(&eepromData), sizeof(EEPROMData));

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
  static constexpr uint16_t getDataSize() { return sizeof(EEPROMData); }

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
    EEPROMData() : crc(0), data() {}
  };
};
#endif // EEPROM_HANDLER_HPP