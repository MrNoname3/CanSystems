#pragma once
// Native-test shim for the FreeRTOS return codes and tick helpers used by the CAN handler.
#include <stdint.h>

using BaseType_t = int32_t;
using TickType_t = uint32_t;

static constexpr BaseType_t pdTRUE = 1;
static constexpr BaseType_t pdFALSE = 0;

// The host queue never blocks, so a tick count only has to survive the arithmetic.
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
