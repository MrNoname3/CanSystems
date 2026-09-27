#pragma once
// Host stand-in for the ESP-IDF ROM GPIO matrix routines. Pin routing carries no observable
// behaviour for the CAN register model, so these record nothing.

#include <stdint.h>

inline void esp_rom_gpio_pad_select_gpio(uint32_t /*pin*/) {}
inline void esp_rom_gpio_connect_in_signal(uint32_t /*pin*/, uint32_t /*signal*/, bool /*invert*/) {}
inline void esp_rom_gpio_connect_out_signal(uint32_t /*pin*/, uint32_t /*signal*/, bool /*invert*/, bool /*invertEnable*/) {}
