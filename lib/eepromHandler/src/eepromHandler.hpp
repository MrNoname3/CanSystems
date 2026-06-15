#ifndef EEPROM_HANDLER_HPP
#define EEPROM_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <EEPROM.h>                                                 /// EEPROM access library.
#include <string.h>                                                 /// memcmp for write verification.
#include "crc16.hpp"                                                /// CRC16 calculation utility.

/// @brief Wrapper class for EEPROM read/write operations with CRC validation.
/// @tparam T Data type to be stored in EEPROM.
/// @tparam eepromAddress EEPROM address where the data will be stored.
template<class T, uint16_t eepromAddress>
class EEPROMHandler final {
public:
  /// @brief Default constructor. Initializes the internal pointer to null.
  EEPROMHandler() :
    data(nullptr) {}

  /// @brief Constructor with a pointer to the data.
  /// @param data Pointer to the data to be stored or retrieved from EEPROM.
  explicit EEPROMHandler(T* data) :
    data(data) {}

  /// @brief Default destructor.
  ~EEPROMHandler() = default;

  /// @brief Saves the data pointed to by the pointer set in the constructor.
  /// @return True if the operation was successful; false otherwise.
  [[nodiscard]] bool save() {
    if(data == nullptr) { return false; }
    return save(data);
  }

  /// @brief Saves the data pointed to by the specified pointer.
  /// @param data Pointer to the data to be stored.
  /// @return True if the operation was successful; false otherwise.
  [[nodiscard]] static bool save(T* data) {
#if defined(ESP8266) || defined(ESP32)
    // cppcheck-suppress knownConditionTrueFalse
    if(!init()) { return false; }
#endif
    if(data == nullptr) { return false; }
    EEPROMData eepromData;
    eepromData.data = *data;
    eepromData.crc = Crc16::calculate(reinterpret_cast<uint8_t*>(&eepromData), sizeof(EEPROMData));

    // Write the data to EEPROM.
    EEPROM.put(eepromAddress, eepromData);
#if defined(ESP8266) || defined(ESP32)
    if(!EEPROM.commit()) { return false; }
#endif

    // Verify the written data.
    EEPROMData readBack;
    EEPROM.get(eepromAddress, readBack);
    return memcmp(&eepromData, &readBack, sizeof(EEPROMData)) == 0;
  }

  /// @brief Loads data into the memory address stored by the pointer set in the constructor.
  /// @return True if the operation was successful; false otherwise.
  [[nodiscard]] bool load() {
    if(data == nullptr) { return false; }
    return load(data);
  }

  /// @brief Loads data into the memory address stored by the specified pointer.
  /// @param data Pointer to the memory where the data will be loaded.
  /// @return True if the operation was successful; false otherwise.
  [[nodiscard]] static bool load(T* data) {
#if defined(ESP8266) || defined(ESP32)
    // cppcheck-suppress knownConditionTrueFalse
    if(!init()) { return false; }
#endif
    if(data == nullptr) { return false; }
    // Read the data from EEPROM.
    EEPROMData eepromData;
    EEPROM.get(eepromAddress, eepromData);

    // Validate CRC.
    uint16_t crcReceived = eepromData.crc;
    eepromData.crc = 0U;
    uint16_t crcCalculated = Crc16::calculate(reinterpret_cast<uint8_t*>(&eepromData), sizeof(EEPROMData));

    if(crcReceived == crcCalculated) {
      *data = eepromData.data;
      return true;  // CRC validation succeeded.
    }
    return false; // CRC validation failed.
  }

  /// @brief Retrieves the size of the data frame stored in EEPROM.
  /// @return Size of the data frame in bytes.
  [[nodiscard]] static constexpr uint16_t getDataSize() { return sizeof(EEPROMData); }

  EEPROMHandler(const EEPROMHandler&) = delete;               // Define copy constructor.
  EEPROMHandler& operator=(const EEPROMHandler&) = delete;    // Define copy assignment operator.
  EEPROMHandler(EEPROMHandler&&) = delete;                    // Define move constructor.
  EEPROMHandler& operator=(EEPROMHandler&&) = delete;         // Define move assignment operator.

private:
#if defined(ESP8266) || defined(ESP32)
  /// @brief Initialize the EEPROM for storing data.
  /// @return `true` if the EEPROM is successfully initialized or was already initialized;
  ///         `false` if the initialization fails.
  static inline bool init() {
    // cppcheck-suppress knownConditionTrueFalse
    if(eepromInitialised) { return true; }
    eepromInitialised = EEPROM.begin(sizeof(EEPROMData));
    return eepromInitialised;
  }
#endif

  /// @brief Data structure representing the stored frame in EEPROM.
  struct __attribute__((packed))
  EEPROMData {
    uint16_t crc = 0U;                              // CRC16 value of the data.
    T data{};                                       // User-defined data type, value-initialized to zero.
    EEPROMData() = default;
  };
#if defined(ESP8266) || defined(ESP32)
  static inline bool eepromInitialised = false;     // Tracks whether the EEPROM has been initialized.
#endif

  T* data;                                          // Pointer to the user-defined data type.
};
#endif // EEPROM_HANDLER_HPP
