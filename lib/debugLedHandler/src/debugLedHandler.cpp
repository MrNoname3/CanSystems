#include "debugLedHandler.hpp"

uint8_t DebugLedHandler::dbgLedOnState = 1U;
uint8_t DebugLedHandler::dbgLedPin = DebugLedHandler::invalidPin;

DebugLedHandler::DebugLedHandler(uint8_t debugLedPin, uint8_t ledOnState)
#if defined(ESP8266) || defined(ESP32)
  : ledTicker()
#endif
{
  setupLedPin(debugLedPin, ledOnState);
}

void DebugLedHandler::setupLedPin(uint8_t debugLedPin, uint8_t ledOnState) {
  dbgLedPin = debugLedPin;
  dbgLedOnState = ledOnState;
  pinMode(dbgLedPin, OUTPUT);
  ledOff();
}

void DebugLedHandler::ledOn() {
  if(dbgLedPin != invalidPin) {
    digitalWrite(dbgLedPin, dbgLedOnState);
  }
}

void DebugLedHandler::ledOff() {
  if(dbgLedPin != invalidPin) {
    digitalWrite(dbgLedPin, static_cast<uint8_t>(!static_cast<bool>(dbgLedOnState)));
  }
}

#if defined(__AVR_ATmega328P__)
void DebugLedHandler::ledToggle() {
  if(dbgLedPin == invalidPin) { return; }
  digitalWrite(dbgLedPin, static_cast<uint8_t>(!static_cast<bool>(digitalRead(dbgLedPin))));
}
#elif defined(ESP8266) || defined(ESP32)
void DebugLedHandler::ledToggle() { // NOLINT(readability-convert-member-functions-to-static)
  if(dbgLedPin != invalidPin) {
    digitalWrite(dbgLedPin, static_cast<uint8_t>(!static_cast<bool>(digitalRead(dbgLedPin))));
  }
}

void DebugLedHandler::startTicker(uint32_t tickIntervalMs) { // NOLINT(readability-convert-member-functions-to-static)
  ledOff();
  ledTicker.attach_ms(tickIntervalMs, ledToggle);
}

void DebugLedHandler::stopTicker() { // NOLINT(readability-convert-member-functions-to-static)
  ledTicker.detach();
  ledOff();
}
#endif