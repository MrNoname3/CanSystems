#pragma once
/// @file canHandler.hpp
/// @brief Conditional inclusion of CAN handler implementations based on the target architecture.
#ifdef ARDUINO_ARCH_AVR
#include "canHandlerAtmega328P.hpp"                                 /// CAN handler for ATmega328P-based Arduino boards.
#elif defined(ESP32)
#include "canHandlerEsp32.hpp"                                      /// CAN handler for ESP32 boards.
#else
#include "canHandlerBase.hpp"                                       /// Base class used on non-hardware platforms (e.g. native tests).
using CanHandler = CanHandlerBase;
#endif