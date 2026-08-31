#pragma once
// Host stand-in for the ESP-IDF GPIO driver. Pin routing carries no observable behaviour for
// the CAN register model, so these record nothing.

#include <stdint.h>

using gpio_num_t = int;

constexpr gpio_num_t GPIO_NUM_4 = 4;
constexpr gpio_num_t GPIO_NUM_5 = 5;

enum gpio_mode_t : uint8_t {
  GPIO_MODE_INPUT = 0U,
  GPIO_MODE_OUTPUT = 1U
};

constexpr uint32_t CAN_TX_IDX = 0U;
constexpr uint32_t CAN_RX_IDX = 0U;

inline void gpio_set_direction(gpio_num_t /*pin*/, gpio_mode_t /*mode*/) {}
inline void gpio_pad_select_gpio(gpio_num_t /*pin*/) {}
inline void gpio_matrix_out(gpio_num_t /*pin*/, uint32_t /*signal*/, bool /*invert*/, bool /*invertEnable*/) {}
inline void gpio_matrix_in(gpio_num_t /*pin*/, uint32_t /*signal*/, bool /*invert*/) {}
