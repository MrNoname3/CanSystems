#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Print.h"

using byte = uint8_t;
using boolean = uint8_t;

enum : uint8_t { LOW = 0U, HIGH = 1U };
enum : uint8_t { INPUT = 0U, OUTPUT = 1U, INPUT_PULLUP = 2U, FALLING = 3U };
#define IRAM_ATTR

extern "C" {

/* sketch */
extern void setup(void);
extern void loop(void);
uint32_t millis(void);

void     pinMode(uint8_t pin, uint8_t mode);
void     digitalWrite(uint8_t pin, uint8_t val);
int      digitalRead(uint8_t pin);
uint16_t analogRead(uint8_t pin);
void     analogWrite(uint8_t pin, int val);
void     attachInterrupt(uint8_t pin, void (*fn)(), uint8_t mode);
void     detachInterrupt(uint8_t pin);
uint8_t  digitalPinToInterrupt(uint8_t pin);
void     cli();
void     sei();
}

void     setFakeMillis(uint32_t t);
void     clearFakeMillis();
void     setAnalogReadValue(uint16_t val);
uint8_t  getDigitalWriteValue(uint8_t pin);
uint8_t  getPinMode(uint8_t pin);
void     resetGpioState();

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte_near
#define pgm_read_byte_near(x) *(x)
#endif
// clang-format off
#define yield(x) {}
// clang-format on
