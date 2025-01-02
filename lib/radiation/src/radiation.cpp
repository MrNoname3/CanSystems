#include "radiation.hpp"

const char Radiation::cpmMessageFrame[] PROGMEM = {
  "{"
    "\"cpm\":%hu"
  "}"
};
volatile uint16_t Radiation::cpm = 0U;
volatile bool Radiation::measureDone = false;
volatile uint16_t Radiation::cpmToSend = 0U;

Radiation::Radiation(Connectivity& connectivity, const char* subtopic, uint8_t sensorPin) :
  MqttBase(connectivity, subtopic),
  measureTicker(),
  sensorPin(sensorPin)
{
  pinMode(sensorPin, INPUT);
}

bool Radiation::init() {
  attachInterrupt(digitalPinToInterrupt(sensorPin), counter, FALLING);
  measureTicker.attach_ms(measureTime, measure);
  cpm = 0U;
  return true;
}

void Radiation::end() {
  detachInterrupt(digitalPinToInterrupt(sensorPin));
  measureTicker.detach();
  cpm = 0U;
  measureDone = false;
}

bool Radiation::run() {
  if(measureDone) {
    measureDone = false;
    char dataOut[dataOutBufSize] = { '\0' };
    const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), cpmMessageFrame, cpmToSend);
    const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
    if(!dataOutValid) { return false; }
    if(!MqttBase::sendMessage(dataOut)) { return false; }
  }
  return true;
}

void Radiation::counter() {
  cpm++;
}

void Radiation::measure() {
  cli();
  cpmToSend = cpm;
  cpm = 0U;
  measureDone = true;
  sei();
}