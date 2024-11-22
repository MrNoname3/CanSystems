#ifndef EEPROM_HANDLER_HPP
#define EEPROM_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <EEPROM.h>                                                 /// EEPROM access library.
#include "crc16.hpp"                                                /// CRC16 calculation utility.

/// @brief Wrapper class for EEPROM read/write operations with CRC validation.
/// @tparam T Data type to be stored in EEPROM.
/// @tparam eepromAddress EEPROM address where the data will be stored.
template<class T, uint16_t eepromAddress>
class EEPROMHandler final {
public:
  /// @brief Default constructor. Initializes the internal pointer to null.
  EEPROMHandler() : data(nullptr) {}

  /// @brief Constructor with a pointer to the data.
  /// @param data Pointer to the data to be stored or retrieved from EEPROM.
  explicit EEPROMHandler(T* data) : data(data) {}

  /// @brief Default destructor.
  virtual ~EEPROMHandler() = default;

  /// @brief Saves the data pointed to by the pointer set in the constructor.
  /// @return True if the operation was successful; false otherwise.
  bool save() {
    if (data == nullptr) { return false; } // Null pointer check.
    return save(data);
  }

  /// @brief Saves the data pointed to by the specified pointer.
  /// @param data Pointer to the data to be stored.
  /// @return True if the operation was successful; false otherwise.
  static bool save(T* data) {
    EEPROMData eepromData;
    eepromData.data = *data;
    eepromData.crc = Crc16::calculate(reinterpret_cast<uint8_t*>(&eepromData), sizeof(EEPROMData));

    // Write the data to EEPROM.
    EEPROM.put(eepromAddress, eepromData);

    // Verify the written data.
    for (uint16_t i = 0; i < sizeof(EEPROMData); ++i) {
      if (EEPROM.read(eepromAddress + i) != reinterpret_cast<uint8_t*>(&eepromData)[i]) {
        return false;  // Write operation failed.
      }
    }
    return true;  // Write operation succeeded.
  }

  /// @brief Loads data into the memory address stored by the pointer set in the constructor.
  /// @return True if the operation was successful; false otherwise.
  bool load() {
    if (data == nullptr) { return false; } ///< Null pointer check.
    return load(data);
  }

  /// @brief Loads data into the memory address stored by the specified pointer.
  /// @param data Pointer to the memory where the data will be loaded.
  /// @return True if the operation was successful; false otherwise.
  static bool load(T* data) {
    // Read the data from EEPROM.
    EEPROMData eepromData;
    EEPROM.get(eepromAddress, eepromData);

    // Validate CRC.
    uint16_t crcReceived = eepromData.crc;
    eepromData.crc = 0;
    uint16_t crcCalculated = Crc16::calculate(reinterpret_cast<uint8_t*>(&eepromData), sizeof(EEPROMData));

    if (crcReceived == crcCalculated) {
      *data = eepromData.data;
      return true;  // CRC validation succeeded.
    } else {
      return false; // CRC validation failed.
    }
  }

  /// @brief Retrieves the size of the data frame stored in EEPROM.
  /// @return Size of the data frame in bytes.
  static constexpr uint16_t getDataSize() { return sizeof(EEPROMData); }

  EEPROMHandler(const EEPROMHandler&) = delete;               // Define copy constructor.
  EEPROMHandler& operator=(const EEPROMHandler&) = delete;    // Define copy assignment operator.
  EEPROMHandler(EEPROMHandler&&) = delete;                    // Define move constructor.
  EEPROMHandler& operator=(EEPROMHandler&&) = delete;         // Define move assignment operator.

private:
  /// @brief Data structure representing the stored frame in EEPROM.
  struct __attribute__((packed))
  EEPROMData {
    uint16_t crc;                               // CRC16 value of the data.
    T data;                                     // User-defined data type.
    EEPROMData() : crc(0), data() {}            // Default constructor initializes members to zero.
  };

  T* data;                                      // Pointer to the user-defined data type.
};
#endif // EEPROM_HANDLER_HPP