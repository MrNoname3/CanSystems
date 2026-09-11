#include "debugLedHandler.hpp"

uint8_t DebugLedHandler::dbgLedOnState = 1U;
uint8_t DebugLedHandler::dbgLedPin = DebugLedHandler::invalidPin;
uint8_t DebugLedHandler::ledState = 0U;

DebugLedHandler::DebugLedHandler(uint8_t debugLedPin, uint8_t ledOnState) {
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
    ledState = dbgLedOnState;
    digitalWrite(dbgLedPin, ledState);
  }
}

void DebugLedHandler::ledOff() {
  if(dbgLedPin != invalidPin) {
    ledState = dbgLedOnState ^ 1U;
    digitalWrite(dbgLedPin, ledState);
  }
}

#if defined(__AVR_ATmega328P__)
void DebugLedHandler::ledToggle() {
  if(dbgLedPin == invalidPin) { return; }
  ledState ^= 1U;
  digitalWrite(dbgLedPin, ledState);
}
#elif defined(ESP8266) || defined(ESP32)
void DebugLedHandler::ledToggle() {
  if(dbgLedPin != invalidPin) {
    ledState ^= 1U;
    digitalWrite(dbgLedPin, ledState);
  }
}

void DebugLedHandler::startTicker(uint32_t tickIntervalMs) {
  ledOff();
  ledTicker.attach_ms(tickIntervalMs, ledToggle);
}

void DebugLedHandler::stopTicker() {
  ledTicker.detach();
  ledOff();
}
#endif
