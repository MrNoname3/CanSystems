#include "externalSensor.hpp"
#include <Arduino.h>

ExternalSensor::ExternalSensor(const uint8_t sensorEnablePin) :
  sensorEnPin(sensorEnablePin) {
  pinMode(sensorEnPin, OUTPUT);
}

void ExternalSensor::on() const {
  digitalWrite(sensorEnPin, HIGH);
}

void ExternalSensor::off() const {
  digitalWrite(sensorEnPin, LOW);
}

bool ExternalSensor::getState() const {
  return static_cast<bool>(digitalRead(sensorEnPin));
}