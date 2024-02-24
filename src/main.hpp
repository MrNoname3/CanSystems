#ifndef MAIN_HPP
#define MAIN_HPP

//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.
#include "canHandler/src/canHandler.hpp"
#include <NeoPixelBus.h>                      /// WS2812 LED driver library.
#include <PushButtonClicks.h>                 /// Pushbutton events library.
#include "CircularBuffer.hpp"                 /// Circular buffer class.
#include "DFPlayer.hpp"                       /// MP3 player driver library.
#include <SI7021.h>                           /// Temperature and humidity sensor driver.
#include <SPIFlash.h>                         /// SPI FLASH module driver.
#include "ota.hpp"                            /// OTA byte stream handler.
#include "serialIR.hpp"

//--- Constants ---//
static constexpr const char* OK_STATE               = ": [ OK ]";   // OK status.
static constexpr const char* ERR_STATE              = ": [ ERR ]";  // Error status.
static constexpr uint8_t RGB_LED_NUM                = 19;           // Number of RGB LED's.

static constexpr uint8_t RGB_PIN                    = 7;            // LED DATA PIN
static constexpr uint8_t LED                        = 4;            // Pin of the LED.
static constexpr uint8_t CAN_CS                     = 10;           // CS pin of the SPI CAN controller.
static constexpr uint8_t CAN_INT                    = 2;            // Interrupt pin of the SPI CAN controller.
static constexpr uint8_t FLASH_CS                   = 8;            // SPI FLASH CS pin.
static constexpr uint8_t BUTTON                     = 20;           // Pushbutton pin. (A6)
static constexpr uint8_t DFP_EN                     = 9;            // DFPlayer switch pin.
static constexpr uint8_t DFP_BUSY                   = 3;            // DFPlayer busy pin.
static constexpr uint8_t DFP_TX                     = 5;            // DFPlayer serial RX pin.
static constexpr uint8_t DFP_RX                     = 6;            // DFPlayer serial TX pin.
static constexpr uint8_t LDR_PIN                    = 21;           // Analog light sensor pin. (A7)
static constexpr uint8_t EXT_SENSOR_EN              = 17;           // External sensor enable pin. (A3)
static constexpr uint8_t RS232_RX                   = 15;           // RS232 serial RX pin. (A2)
static constexpr uint8_t RS232_TX                   = 16;           // RS232 serial TX pin. (A1)

//--- Structs ---//

/// @brief Color values for RGB LED.
struct __attribute__((packed))
RGBValues {
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
};

//--- Enums ---//

/// @brief Base command list for nodes.
enum class CanCmd : uint16_t {
  PING = 0,                                   // Ping command.
  RESTART,                                    // Node reset command.
  BUTTON_EVENT,                               // Get button event type.
  FILET_START,                                // Init file transfer process.
  FILET_SEND,                                 // Send file pieces.
  FILET_END,                                  // File transfer process ended.

  RGB_LED,                                    // Set WS2812 RGB LED color.
  PLAY_MP3,                                   // Play MP3 file.
  READ_HUM_TEMP_LDR,                          // Read humidity, temperature and light value.
};

/// @brief Error types.
enum class errorTypes : uint8_t {
  ERR_I2C_READ_TIMEOUT = 0,                   // I2C read timeout.
  ERR_I2C_SENSOR_INIT,                        // I2C sensor init failed.
  ERR_CAN_INIT,                               // CAN init failed.
  ERR_CAN_ID_SET,                             // CAN ID setup method failed.
  ERR_CAN_DATA_WRITE,                         // CAN data write method failed.
  ERR_CAN_ENDPACKET,                          // CAN endpacket method failed.
  ERR_UNHANDLED_COMMAND,                      // Unhandled command in state machine.

  LAST_ELEMENT                                // Last element of enum!
};

/// @brief States of SI7021 reads.
enum class si7021States : uint8_t {
  IDLE = 0,
  READ_TEMPERATURE,
  READ_HUMIDITY,

  LAST_ELEMENT
};

//--- Functions ---//

/// @brief Handles the I2C humidity&temperature sensor and the analog light sensor.
inline void handleSensors() __attribute__((always_inline));

/// @brief Put the given data to RGB LED queue.
/// @param red Value of red color: 0-255.
/// @param green Value of green color: 0-255.
/// @param blue Value of blue color: 0-255.
void addToRGBQueue(const uint8_t red, const uint8_t green, const uint8_t blue);

/// @brief Calculates the 16bit CRC (XModem) of the given data.
/// @param data Data whose CRC value should be calculated.
/// @param length Given data length in bytes.
/// @return Returns with the calculated CRC value.
uint16_t calculateCRC16(const uint8_t* data, uint16_t length);

inline int analogReadFast(uint8_t ADCpin) __attribute__((always_inline));

#endif // MAIN_HPP
