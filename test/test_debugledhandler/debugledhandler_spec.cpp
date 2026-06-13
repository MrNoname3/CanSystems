#include "debugLedHandler.hpp"
#include "Arduino.h"
#include "BDDTest.h"

// DebugLedHandler keeps its pin/state in static members (one logical LED per firmware), so each
// test reconstructs the handler after resetGpioState() to rebind the pin cleanly.
static constexpr uint8_t LED_PIN = 4U;

bool test_constructor_configures_pin_and_starts_off() {
  IT("the constructor sets the pin to output and leaves the LED off");
  resetGpioState();
  DebugLedHandler led(LED_PIN, HIGH);
  IS_EQUAL(getPinMode(LED_PIN), OUTPUT);
  IS_EQUAL(getDigitalWriteValue(LED_PIN), LOW);     // active-high: off == LOW
  END_IT
}

bool test_active_high_on_off() {
  IT("with an active-high LED, ledOn drives HIGH and ledOff drives LOW");
  resetGpioState();
  DebugLedHandler led(LED_PIN, HIGH);
  DebugLedHandler::ledOn();
  IS_EQUAL(getDigitalWriteValue(LED_PIN), HIGH);
  DebugLedHandler::ledOff();
  IS_EQUAL(getDigitalWriteValue(LED_PIN), LOW);
  END_IT
}

bool test_active_low_inverts_levels() {
  IT("with an active-low LED, ledOn drives LOW and ledOff drives HIGH");
  resetGpioState();
  DebugLedHandler led(LED_PIN, LOW);
  DebugLedHandler::ledOn();
  IS_EQUAL(getDigitalWriteValue(LED_PIN), LOW);
  DebugLedHandler::ledOff();
  IS_EQUAL(getDigitalWriteValue(LED_PIN), HIGH);
  END_IT
}

int main() {
  SUITE("DebugLedHandler");
  test_constructor_configures_pin_and_starts_off();
  test_active_high_on_off();
  test_active_low_inverts_levels();
  FINISH
}
