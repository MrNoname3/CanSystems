#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Print.h"

using byte = uint8_t;
using boolean = uint8_t;
// NOLINTNEXTLINE(bugprone-reserved-identifier) intentionally mirrors the Arduino core's reserved name
using __FlashStringHelper = char;          // Flash strings are plain RAM strings on the host (F(x) is identity).

enum : uint8_t { LOW = 0U, HIGH = 1U };
enum : uint8_t { INPUT = 0U, OUTPUT = 1U, INPUT_PULLUP = 2U, FALLING = 3U, RISING = 4U };
enum : uint8_t { BIN = 2U, OCT = 8U, DEC = 10U, HEX = 16U };
#define IRAM_ATTR
#define bitSet(value, bit) ((value) |= (1UL << (bit)))

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
void     noInterrupts();
void     interrupts();
}

void     setFakeMillis(uint32_t t);
void     clearFakeMillis();
void     setAnalogReadValue(uint16_t val);
uint8_t  getDigitalWriteValue(uint8_t pin);
uint8_t  getPinMode(uint8_t pin);
void     triggerInterrupt(uint8_t pin);    // Fires the handler stored by attachInterrupt(), if any.
void     resetGpioState();

extern uint8_t EIFR;                       // AVR external interrupt flag register stand-in (dfPlayer).

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef F
#define F(x) (x)
#endif
#ifndef FPSTR
#define FPSTR(x) (x)
#endif
#ifndef pgm_read_byte_near
#define pgm_read_byte_near(x) *(x)
#endif
// clang-format off
#define yield(x) {}
// clang-format on

// The real Arduino.h provides Stream and the global Serial; mirror that for vendored
// libraries that rely on it (DFPlayerMiniFast). Safe against include cycles via pragma once.
#include "Stream.h"
#include "HardwareSerial.h"
