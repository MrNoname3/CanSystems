#ifndef MAIN_HPP
#define MAIN_HPP

//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.
#include "canHandler/src/canHandler.hpp"
#include <NeoPixelBus.h>                      /// WS2812 LED driver library.
#include <PushButtonClicks.h>                 /// Pushbutton events library.
#include "DFPlayer.hpp"                       /// MP3 player driver library.
#include "ambientSensor/src/ambientSensor.hpp"
//#include "ota.hpp"                            /// OTA byte stream handler.
#include "serialIR.hpp"

//--- Constants ---//
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
  RGB_LED,                                    // Set WS2812 RGB LED color.
  PLAY_MP3,                                   // Play MP3 file.
  READ_HUM_TEMP_LDR,                          // Read humidity, temperature and light value.
};

//--- Functions ---//
void canMessageArrived(uint16_t command, const uint8_t (&data)[8]);

/// @brief Send the given data to the RGB LED(s).
/// @param red Value of red color: 0-255.
/// @param green Value of green color: 0-255.
/// @param blue Value of blue color: 0-255.
void setRgbLed(const uint8_t red, const uint8_t green, const uint8_t blue);

#endif // MAIN_HPP