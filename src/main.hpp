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
#include "serialIR.hpp"

//--- Constants ---//
static constexpr uint8_t RGB_LED_NUM                = 19U;          // Number of RGB LED's.
static constexpr uint8_t RGB_PIN                    = 7U;           // LED DATA PIN
static constexpr uint8_t LED_PIN                    = 4U;           // Pin of the LED.
static constexpr uint8_t CAN_CS                     = 10U;          // CS pin of the SPI CAN controller.
static constexpr uint8_t CAN_INT                    = 2U;           // Interrupt pin of the SPI CAN controller.
static constexpr uint8_t FLASH_CS                   = 8U;           // SPI FLASH CS pin.
static constexpr uint8_t BUTTON_PIN                 = 20U;          // Pushbutton pin. (A6)
static constexpr uint8_t DFP_EN                     = 9U;           // DFPlayer switch pin.
static constexpr uint8_t DFP_BUSY                   = 3U;           // DFPlayer busy pin.
static constexpr uint8_t DFP_TX                     = 5U;           // DFPlayer serial RX pin.
static constexpr uint8_t DFP_RX                     = 6U;           // DFPlayer serial TX pin.
static constexpr uint8_t LDR_PIN                    = 21U;          // Analog light sensor pin. (A7)
static constexpr uint8_t EXT_SENSOR_EN              = 17U;          // External sensor enable pin. (A3)
static constexpr uint8_t RS232_RX                   = 15U;          // RS232 serial RX pin. (A2)
static constexpr uint8_t RS232_TX                   = 16U;          // RS232 serial TX pin. (A1)

//--- Functions ---//
void canMessageArrived(uint16_t command, const uint8_t (&data)[8]);

void btnEventHandling(const uint8_t& event);

#endif // MAIN_HPP