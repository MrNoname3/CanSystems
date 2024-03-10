#ifndef MAIN_HPP
#define MAIN_HPP

//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.
#include "connectivity/src/connectivity.hpp"
#include "radiation/src/radiation.hpp"
#include "rfHandler/src/rfHandler.hpp"
#include "canHandler/src/canHandler.hpp"
#include "canAlertDriver/src/canAlertDriver.hpp"

//--- Constants ---//
#ifdef ESP8266
static constexpr const uint8_t LED                    = D8;           // Status LED.
static constexpr const uint8_t SPI_CS                 = D0;           // Ethernet shield SPI CS.
#ifdef PROJECT_RAD_RF
static constexpr const uint8_t RAD                    = D2;           // Radiation meter.
static constexpr const uint8_t RF_RX                  = D1;           // RF transmit pin.
static constexpr const uint8_t RF_TX                  = D3;           // RF receive pin.
#endif
#elif defined ESP32
static constexpr const uint8_t LED                    = 2;            // Status LED.
static constexpr const uint8_t SPI_CS                 = -1;           // Ethernet shield SPI CS.
#endif

//--- Functions ---//
#if defined(ESP32) && defined(PROJECT_CAN)
void canTask(void *pvParameters);
#endif

#endif // MAIN_HPP