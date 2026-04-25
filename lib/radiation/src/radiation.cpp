#include "radiation.hpp"

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

bool Radiation::publishDiscovery() {
  return doPublishEntityDiscovery({Connectivity::HADiscovery::EntityType::sensor, discEntityFields});
}

bool Radiation::init() { // NOLINT(readability-convert-member-functions-to-static,readability-make-member-function-const)
  attachInterrupt(digitalPinToInterrupt(sensorPin), counter, FALLING);
  measureTicker.attach_ms(measureTime, measure);
  cpm = 0U;
  return true;
}

void Radiation::end() { // NOLINT(readability-make-member-function-const)
  detachInterrupt(digitalPinToInterrupt(sensorPin));
  measureTicker.detach();
  cpm = 0U;
  measureDone = false;
}

bool Radiation::run() { // NOLINT(readability-convert-member-functions-to-static)
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

void Radiation::counter() { // NOLINT(readability-convert-member-functions-to-static)
  cpm++;
}

void Radiation::measure() { // NOLINT(readability-convert-member-functions-to-static)
  cli();
  cpmToSend = cpm;
  cpm = 0U;
  measureDone = true;
  sei();
}