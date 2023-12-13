#ifndef MAIN_HPP
#define MAIN_HPP

//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.
#include <Ticker.h>                           /// Timer interrupt hadnler.
#include "connectivity.hpp"
#include "radiation.hpp"

//--- Constants ---//
static const char SW_VERSION[] PROGMEM                = "V1.0.0";                     // Actual software version.

static constexpr const uint8_t LED                    = D8;           // Status LED.
static constexpr const uint8_t SPI_CS                 = D0;           // Ethernet shield SPI CS.
static constexpr const uint8_t RAD                    = D2;           // Radiation meter.

#define LED_T (GPO  ^=  (1 << LED))                 // LED pin toggle.
#define LED_H (GPOS |=  (1 << LED))                 // LED pin high.
#define LED_L (GPOC |=  (1 << LED))                 // LED pin low.
#define NOP __asm__("nop\n\t");                     // 1 CPU cycle delay.

// Monitor the internal VCC level, it varies with WiFi load.
// Don't connect anything to the analog input pin!
ADC_MODE(ADC_VCC);

//--- Structs ---//

//--- Enums ---//

//--- Functions ---//

/// @brief 
/// @param  
void tick(void);

#endif // MAIN_HPP