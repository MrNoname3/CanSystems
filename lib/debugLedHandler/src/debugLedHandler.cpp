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
    digitalWrite(dbgLedPin, !dbgLedOnState);
  }
}

#if defined(__AVR_ATmega328P__)
void DebugLedHandler::ledToggle() {
  if(dbgLedPin == invalidPin) {return;}
  digitalWrite(dbgLedPin, !digitalRead(dbgLedPin));
}
#elif defined(ESP8266) || defined(ESP32)
void DebugLedHandler::ledToggle() {
  if(dbgLedPin != invalidPin) {
    digitalWrite(dbgLedPin, !digitalRead(dbgLedPin));
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