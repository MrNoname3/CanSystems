#pragma once

#if defined(ARDUINO_ARCH_ESP32)
#include "ESP32SJA1000.h"                                           /// Built-in CAN controller of the ESP32.
#else
#include "MCP2515.h"                                                /// SPI CAN controller used on every other target.
#endif
