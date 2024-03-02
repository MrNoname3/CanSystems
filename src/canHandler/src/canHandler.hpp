#ifndef CAN_HANDLER_HPP
#define CAN_HANDLER_HPP

#include <stdint.h>
#include <HardwareSerial.h>
#include "canCommands.hpp"
#include "../../eepromHandler/src/eepromHandler.hpp"                /// EEPROM wrapper class.
#include <SPIFlash.h>                                               /// SPI FLASH module driver.
#include "../../ota/src/ota.hpp"

class CanHandler final {
public:
  static constexpr const char* OK_STATE               = "[OK]";   // OK status.
  static constexpr const char* ERR_STATE              = "[ERR]";  // Error status.

  struct __attribute__((packed))
  CanFrame {                                    // CAN frame.
    union {
      uint32_t extId;                           // Extended CAN ID.
      struct {
        uint32_t to : 10;                       // 10 bits for receiver address.
        uint32_t cmd : 9;                       // 9 bits for command.
        uint32_t from : 10;                     // 10 bits for sender address.
        uint32_t padding : 3;                   // Padding to fill up to 32 bits.
      };
    };
    uint8_t data[8];                            // CAN data.
    CanFrame() : extId(), data{0} {}
  };

  CanHandler(HardwareSerial& serial, uint8_t canCsPin, uint8_t canIntPin, uint8_t ledPin, uint8_t flashCsPin);
  /// @brief Destructor of the object.
  ~CanHandler() = default;
  void begin(uint32_t canBaud);
  void loop();
  bool send(uint16_t command, const uint8_t (&data)[8]) const;
  bool send(uint16_t command) const;
  bool send(CanCmd command, const uint8_t (&data)[8]) const;
  bool send(CanCmd command) const;
  void ledOn();
  void ledOff();
  void ledToggle();
  /// @brief Reset the MCU.
  void restartMCU();
  void addCanCallback(void (*canCallback)(uint16_t command, const uint8_t (&data)[8]));

  CanHandler(const CanHandler&) = delete;                       // Define copy constructor.
  CanHandler& operator=(const CanHandler&) = delete;            // Define copy assignment operator.
  CanHandler(CanHandler&&) = delete;                            // Define move constructor.
  CanHandler& operator=(CanHandler&&) = delete;                 // Define move assignment operator.
private:
  inline bool beginSimple(uint32_t canBaud);
  inline bool loopSimple();
  static inline void rxInterrupt();

  static constexpr uint16_t masterCanId = 10U;
  static constexpr uint8_t rxBufferSize = 5;
  static constexpr uint16_t pingTime = 1500;                    // Ping timeot time in ms.
  static constexpr uint16_t flashJedecId = 0xEF40U;             // SPI FLASH driver. (0xEF40 -> Windbond 64mbit flash.)
  HardwareSerial& serialPort;
  uint16_t localCanId;
  EEPROMHandler<uint16_t, 0> eepromHandler;
  const uint8_t ledPin;
  SPIFlash flash;
  static volatile uint8_t intCount;
  void (*canCallback)(uint16_t command, const uint8_t (&data)[8]) = nullptr;
  static constexpr uint8_t otaFlashBegin = 0;                   // Flash begin address for OTA.
  static constexpr uint8_t otaFwPiece = 4;                      // Size of FW chunks in bytes.
  OTA<otaFlashBegin, otaFwPiece> ota  ;                         // OTA handler.

};
#endif // CAN_HANDLER_HPP