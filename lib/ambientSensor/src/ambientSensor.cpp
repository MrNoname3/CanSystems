#include "ambientSensor.hpp"
#include <Wire.h>
#include "common.hpp"

AmbientSensor::AmbientSensor(CanHandler& canHandler, uint8_t lightPin, uint32_t measurePeriod) :
  si7021(),
  canHandler(canHandler),
  lightPin(lightPin),
  measurePeriod(measurePeriod),
  lightValue(0U),
  eventTimer(0U)
{
  Wire.setClock(400000U);                                                     // Set I2C bus speed.
  Wire.setWireTimeout(20000U, true);                                          // Set I2C timeout to 20ms.
}

bool AmbientSensor::init() {
  const bool si7021BeginResult = si7021.begin();
  if(si7021BeginResult) { si7021.setHeater(false); }
  eventTimer = millis();
  return si7021BeginResult;
}

void AmbientSensor::run() {
  const uint32_t actualTime = millis();
  filterAnalogValue();
  if(!si7021.sensorExists()) { return; }
  if(Time::hasElapsed(actualTime, eventTimer, measurePeriod)) {
    eventTimer = actualTime;
    const int16_t temperature = si7021.getCelsiusHundredths();
    const uint16_t humidity = si7021.getHumidityPercent();
    const bool dataInvalid = Wire.getWireTimeoutFlag();
    if(dataInvalid) {
      Wire.clearWireTimeoutFlag();
      return;
    }
    const uint8_t data[8] = {
      static_cast<uint8_t>((temperature >> 0U) & 0xFF),
      static_cast<uint8_t>((temperature >> 8U) & 0xFF),
      static_cast<uint8_t>((humidity >> 0U) & 0xFF),
      static_cast<uint8_t>((humidity >> 8U) & 0xFF),
      static_cast<uint8_t>((lightValue >> 0U) & 0xFF),
      static_cast<uint8_t>((lightValue >> 8U) & 0xFF),
      0U,
      0U
    };
    canHandler.send(CanCmd::READ_HUM_TEMP_LDR, data);
  }
}

void AmbientSensor::filterAnalogValue() {
  // Complement filter calculation.
  static constexpr uint8_t adcInputFilterAlpha = 10U;     // Complement filter ALPHA value.
  const uint16_t rawAnalogValue = static_cast<uint16_t>(analogRead(lightPin));
  lightValue = ((adcInputFilterAlpha * rawAnalogValue) + (100U - adcInputFilterAlpha) * (uint32_t)lightValue) / 100U;
}