#include "radiation.hpp"

volatile uint16_t Radiation::cpm = 0;
volatile bool Radiation::measureDone = false;
volatile uint16_t Radiation::cpmToSend = 0;
const char Radiation::CPM_MSG_FRAME[] PROGMEM = {
  "{"
    "\"Time\":\"%s\","
    "\"cpm\":%hu"
  "}"
};

Radiation::Radiation(Connectivity& connectivity, const char* classID, uint8_t sensorPin) :
  MqttComBase(connectivity, classID),
  sensorPin(sensorPin)
{
  pinMode(sensorPin, INPUT);
}

bool Radiation::begin() {
  constexpr const uint16_t measureTime = 60000;  //millisec
  attachInterrupt(digitalPinToInterrupt(sensorPin), counter, FALLING);
  measureTimer.attach_ms(measureTime, measure);
  cpm = 0;
  return true;
}

void Radiation::end() {
  detachInterrupt(digitalPinToInterrupt(sensorPin));
  measureTimer.detach();
  cpm = 0;
  measureDone = false;
}

bool Radiation::loop() {
  if(measureDone) {
    measureDone = false;
    char dataOut[dataOutBufSize] = { '\0' };
    const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), CPM_MSG_FRAME, MqttComBase::getIsoTime(), cpmToSend);
    const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
    if(!dataOutValid) { return false; }
    MqttComBase::messageSend(dataOut);
  }
  return true;
}

void Radiation::messageReceived(uint8_t* payload, uint32_t length) {}

void Radiation::counter() { cpm++; }

void Radiation::measure() {
  cli();
  cpmToSend = cpm;
  cpm = 0;
  measureDone = true;
  sei();
}