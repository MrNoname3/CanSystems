#include "ambientSensor.hpp"
#include "common.hpp"

AmbientSensor::AmbientSensor(CanHandler& canHandler, uint8_t lightPin, uint32_t measurePeriod) :
  si7021(Time::msToUs(10U)),
  canHandler(canHandler),
  lightPin(lightPin),
  measurePeriod(measurePeriod),
  lightValue(0U),
  temperature(0),
  humidity(0U),
  eventTimer(0U),
  event(Event::IDLE)
{}

bool AmbientSensor::init() {
  const bool si7021BeginResult = si7021.init();
  if(si7021BeginResult) {
    si7021.setHeater(false);
    si7021.setPrecision(SI7021::Precision::T11RH11);
  }
  eventTimer = millis();
  return si7021BeginResult;
}

void AmbientSensor::run() {
  const uint32_t actualTime = millis();
  lightValue = Analog::complementaryFilter10(static_cast<uint16_t>(analogRead(lightPin)), lightValue);
  switch(event) {
    case Event::IDLE: {
      if(Time::hasElapsed(actualTime, eventTimer, measurePeriod)) {
        eventTimer = actualTime;
        event = Event::READ_TEMPERATURE;
      };
    } break;
    case Event::READ_TEMPERATURE: {
      event = si7021.getCelsiusHundredths(temperature) ? Event::READ_HUMIDITY : Event::SENSOR_ERROR;
    } break;
    case Event::READ_HUMIDITY: {
      event = si7021.getHumidityPercent(humidity) ? Event::SEND_VALUES : Event::SENSOR_ERROR;
    } break;
    case Event::SEND_VALUES: {
      canHandler.send(CanCmd::READ_HUM_TEMP_LDR, (const uint8_t[8]){
        static_cast<uint8_t>((temperature >> 0U) & 0xFF),
        static_cast<uint8_t>((temperature >> 8U) & 0xFF),
        static_cast<uint8_t>((humidity >> 0U) & 0xFF),
        static_cast<uint8_t>((humidity >> 8U) & 0xFF),
        static_cast<uint8_t>((lightValue >> 0U) & 0xFF),
        static_cast<uint8_t>((lightValue >> 8U) & 0xFF),
        0U,
        0U
      });
      event = Event::IDLE;
    } break;
    case Event::SENSOR_ERROR: {
      canHandler.send(CanCmd::HUM_TEMP_SENSOR_ERROR);
      event = Event::IDLE;
    } break;
  };
}