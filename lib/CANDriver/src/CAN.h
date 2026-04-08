#pragma once

#if defined(ARDUINO_ARCH_ESP32)
#include "ESP32SJA1000.h"
#else
#include "MCP2515.h"
#endif
