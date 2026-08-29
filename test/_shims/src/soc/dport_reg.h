#pragma once
// Host stand-in for the ESP32 DPORT registers. Clock gating and peripheral reset have no
// observable effect on the model, so the accessors expand to nothing.

#include <stdint.h>

constexpr uint32_t DPORT_PERIP_CLK_EN_REG = 0U;
constexpr uint32_t DPORT_PERIP_RST_EN_REG = 0U;
constexpr uint32_t DPORT_CAN_CLK_EN = 0U;
constexpr uint32_t DPORT_CAN_RST = 0U;

#define DPORT_SET_PERI_REG_MASK(reg, mask) ((void)(reg), (void)(mask))
#define DPORT_CLEAR_PERI_REG_MASK(reg, mask) ((void)(reg), (void)(mask))
