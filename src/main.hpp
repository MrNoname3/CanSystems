#ifndef MAIN_HPP
#define MAIN_HPP

//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.
#include "connectivity.hpp"
#include "radiation/src/radiation.hpp"
#include "rfHandler/src/rfHandler.hpp"

//--- Constants ---//
static constexpr const uint8_t LED                    = D8;           // Status LED.
static constexpr const uint8_t SPI_CS                 = D0;           // Ethernet shield SPI CS.
static constexpr const uint8_t RAD                    = D2;           // Radiation meter.
static constexpr const uint8_t RF_RX                  = D1;           // RF transmit pin.
static constexpr const uint8_t RF_TX                  = D3;           // RF receive pin.

//--- Structs ---//

//--- Enums ---//

//--- Functions ---//

#endif // MAIN_HPP