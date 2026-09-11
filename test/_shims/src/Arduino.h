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

enum : uint8_t {
  LOW = 0U,
  HIGH = 1U
};

enum : uint8_t {
  INPUT = 0U,
  OUTPUT = 1U,
  INPUT_PULLUP = 2U,
  FALLING = 3U,
  RISING = 4U,
  CHANGE = 5U
};

enum : uint8_t {
  BIN = 2U,
  OCT = 8U,
  DEC = 10U,
  HEX = 16U
};

#define IRAM_ATTR
#define bitSet(value, bit) ((value) |= (1UL << (bit)))

extern "C" {

/* sketch */
extern void setup(void);
extern void loop(void);
uint32_t millis(void);
uint32_t micros(void);
// Nothing is stalled: the pulse recorder below is what a test reads the durations back from.
void delayMicroseconds(uint32_t us);

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
uint16_t analogRead(uint8_t pin);
void analogWrite(uint8_t pin, int val);
void attachInterrupt(uint8_t pin, void (*fn)(), uint8_t mode);
void detachInterrupt(uint8_t pin);
int16_t digitalPinToInterrupt(uint8_t pin);   // signed: the AVR macro answers NOT_AN_INTERRUPT (-1)
void cli();
void sei();
void noInterrupts();
void interrupts();
}

void setFakeMillis(uint32_t t);
void clearFakeMillis();
void setFakeMicros(uint32_t t);
void clearFakeMicros();
void setAnalogReadValue(uint16_t val);
uint8_t getDigitalWriteValue(uint8_t pin);
uint8_t getPinMode(uint8_t pin);
void triggerInterrupt(uint8_t pin);    // Fires the handler stored by attachInterrupt(), if any.
void resetGpioState();

/// @brief One level the pin was driven to, and how long it was held there.
struct Pulse {
  uint8_t level = 0U;                      // LOW or HIGH.
  uint32_t microseconds = 0U;              // Sum of the waits before the next edge.
};

/// @brief Starts recording the pin's edges and the waits between them, discarding any earlier
/// recording. digitalWrite() opens a pulse; every delayMicroseconds() adds to the open one.
void recordPulsesOn(uint8_t pin);
/// @brief Stops recording, leaving what was recorded readable.
void stopRecordingPulses();
/// @brief What the recorded pin was driven to, in order.
const Pulse* recordedPulses();
/// @brief How many pulses that is.
uint32_t recordedPulseCount();

/// @brief Stand-in for an AVR interrupt flag register: a bit is cleared by writing a one to it.
/// @details Plain storage would make `EIFR = bit` and `EIFR |= bit` behave alike, which on the
/// part they do not - the read-modify-write writes back every flag it just read, and so clears
/// each of them. Modelling that is what lets a test tell the two apart.
class FlagRegister final {
public:
  /// @brief Writing ones clears the flags they name and leaves the rest standing.
  FlagRegister& operator=(uint8_t written) {
    flags = static_cast<uint8_t>(flags & ~written);
    return *this;
  }

  /// @brief The read-modify-write a bit-set expands to, which writes back the flags it read.
  FlagRegister& operator|=(uint32_t written) {
    return *this = static_cast<uint8_t>(flags | written);
  }

  operator uint8_t() const { return flags; }

  /// @brief The hardware raising a flag, which no write of ours can do.
  void raise(uint8_t bit) { flags = static_cast<uint8_t>(flags | bit); }

  /// @brief Puts the register back to its powered-up state, for a test starting over.
  void clearAll() { flags = 0U; }

private:
  uint8_t flags = 0U;
};

extern FlagRegister EIFR;                  // AVR external interrupt flag register stand-in (dfPlayer).

// The AVR core's Arduino.h pulls pgmspace.h in, so a library using its macros without saying so
// compiles there; mirror that here rather than making such a library include it for the host.
#include "pgmspace.h"
#ifndef F
#define F(x) (x)
#endif
#ifndef FPSTR
#define FPSTR(x) (x)
#endif
// Waiting has no meaning on the host: tests drive time through setFakeMillis() instead, so
// these do nothing rather than actually stalling the suite.
// clang-format off
#define yield(x) {}
#define delay(x) {}
// clang-format on

// The real Arduino.h provides Stream and the global Serial; mirror that for vendored
// libraries that rely on it (DFPlayerMiniFast). Safe against include cycles via pragma once.
#include "Stream.h"
#include "HardwareSerial.h"
