#ifndef MAIN_HPP
#define MAIN_HPP

//--- Headers ---//
#include <Arduino.h>                                                /// Arduino libraries header.
#include "canHandler/src/canHandler.hpp"                            /// CAN handler library.
#include "rgbLedWrapper/src/rgbLedWrapper.hpp"                      /// RGB LED driver wrapper.
#include "pushButtonHandler/src/pushButtonHandler.hpp"              /// Pushbutton events library.
#include "dfPlayer/src/dfPlayer.hpp"                                /// MP3 player driver library.
#include "ambientSensor/src/ambientSensor.hpp"                      /// Sensor handelr library.
#include "externalSensor/src/externalSensor.hpp"                    /// External temperature and humidity sensor library.
//#include "ota.hpp"                                                  /// OTA byte stream handler.
#include "serialIR.hpp"

//--- Constants ---//
static constexpr uint8_t RGB_LED_NUM                = 19;           // Number of RGB LED's.
static constexpr uint8_t RGB_PIN                    = 7;            // LED DATA PIN
static constexpr uint8_t LED_PIN                    = 4;            // Pin of the LED.
static constexpr uint8_t CAN_CS                     = 10;           // CS pin of the SPI CAN controller.
static constexpr uint8_t CAN_INT                    = 2;            // Interrupt pin of the SPI CAN controller.
static constexpr uint8_t FLASH_CS                   = 8;            // SPI FLASH CS pin.
static constexpr uint8_t BUTTON_PIN                 = 20;           // Pushbutton pin. (A6)
static constexpr uint8_t DFP_EN                     = 9;            // DFPlayer switch pin.
static constexpr uint8_t DFP_BUSY                   = 3;            // DFPlayer busy pin.
static constexpr uint8_t DFP_TX                     = 5;            // DFPlayer serial RX pin.
static constexpr uint8_t DFP_RX                     = 6;            // DFPlayer serial TX pin.
static constexpr uint8_t LDR_PIN                    = 21;           // Analog light sensor pin. (A7)
static constexpr uint8_t EXT_SENSOR_EN              = 17;           // External sensor enable pin. (A3)
static constexpr uint8_t RS232_RX                   = 15;           // RS232 serial RX pin. (A2)
static constexpr uint8_t RS232_TX                   = 16;           // RS232 serial TX pin. (A1)

//--- Functions ---//
void canMessageArrived(uint16_t command, const uint8_t (&data)[8]);

void btnEventHandling(const uint8_t& event);

#endif // MAIN_HPP