#ifndef MAIN_HPP
#define MAIN_HPP

//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.
#include "connectivity.hpp"
#include "radiation.hpp"

//--- Constants ---//
static const char SW_VERSION[] PROGMEM                = "V1.0.0";                     // Actual software version.

static constexpr const uint8_t LED                    = D8;           // Status LED.
static constexpr const uint8_t SPI_CS                 = D0;           // Ethernet shield SPI CS.
static constexpr const uint8_t RAD                    = D2;           // Radiation meter.

// Monitor the internal VCC level, it varies with WiFi load.
// Don't connect anything to the analog input pin!
ADC_MODE(ADC_VCC);

//--- Structs ---//

//--- Enums ---//

//--- Functions ---//

#endif // MAIN_HPP