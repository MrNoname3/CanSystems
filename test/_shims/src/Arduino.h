#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Print.h"

using byte = uint8_t;
using boolean = uint8_t;

extern "C" {

/* sketch */
extern void setup(void);
extern void loop(void);
uint32_t millis(void);
}

void setFakeMillis(uint32_t t);
void clearFakeMillis();

#define PROGMEM
#define pgm_read_byte_near(x) *(x)
// clang-format off
#define yield(x) {}
// clang-format on
