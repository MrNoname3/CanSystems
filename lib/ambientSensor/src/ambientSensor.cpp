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
  lastSentTemperature(INT16_MIN),
  lastSentHumidity(UINT16_MAX),
  lastSentLight(UINT16_MAX),
  eventTimer(0U),
  sendThrottleTimer(0U),
  event(Event::IDLE) {}

bool AmbientSensor::init() {
  const bool si7021BeginResult = si7021.init();
  if(si7021BeginResult) {
    si7021.setHeater(false);
    si7021.setPrecision(SI7021::Precision::T11RH11);
  }
  const uint32_t now = millis();
  eventTimer = now;
  sendThrottleTimer = now;
  return si7021BeginResult;
}

bool AmbientSensor::run() {
  const uint32_t actualTime = millis();
  lightValue = Analog::complementaryFilter10(static_cast<uint16_t>(analogRead(lightPin)), lightValue);
  switch(event) {
    case Event::IDLE: {
      if(Time::hasElapsed(actualTime, eventTimer, measurePeriod)) {
        eventTimer = actualTime;
        event = Event::READ_TEMPERATURE;
      }
    } break;
    case Event::READ_TEMPERATURE: {
      event = si7021.getCelsiusHundredths(temperature) ? Event::READ_HUMIDITY : Event::SENSOR_ERROR;
    } break;
    case Event::READ_HUMIDITY: {
      event = si7021.getHumidityPercent(humidity) ? Event::CHECK_SEND : Event::SENSOR_ERROR;
    } break;
    case Event::CHECK_SEND: {
      int32_t tDiff = static_cast<int32_t>(temperature) - static_cast<int32_t>(lastSentTemperature);
      if(tDiff < 0) { tDiff = -tDiff; }
      uint16_t hDiff = humidity > lastSentHumidity ? humidity - lastSentHumidity : lastSentHumidity - humidity;
      uint16_t lDiff = lightValue > lastSentLight ? lightValue - lastSentLight : lastSentLight - lightValue;
      bool changed = tDiff > kTempTolerance || hDiff > kHumTolerance || lDiff > kLightTolerance;
      event = (changed || Time::hasElapsed(actualTime, sendThrottleTimer, kSendMaxPeriod)) ? Event::SEND_VALUES : Event::IDLE;
    } break;
    case Event::SEND_VALUES: {
      const uint8_t payload[8] = {
        static_cast<uint8_t>(temperature & 0xFF),
        static_cast<uint8_t>((temperature >> 8U) & 0xFF),
        static_cast<uint8_t>(humidity & 0xFF),
        static_cast<uint8_t>((humidity >> 8U) & 0xFF),
        static_cast<uint8_t>(lightValue & 0xFF),
        static_cast<uint8_t>((lightValue >> 8U) & 0xFF),
        0U,
        0U
      };
      canHandler.send(CanCmd::READ_HUM_TEMP_LDR, payload);
      lastSentTemperature = temperature;
      lastSentHumidity = humidity;
      lastSentLight = lightValue;
      sendThrottleTimer = actualTime;
      event = Event::IDLE;
    } break;
    case Event::SENSOR_ERROR: {
      canHandler.send(CanCmd::HUM_TEMP_SENSOR_ERROR);
      event = Event::IDLE;
    } break;
  }
  return true;
}
