#include "externalSensor.hpp"
#include "Arduino.h"
#include "BDDTest.h"

static constexpr uint8_t EN_PIN = 17U;

bool test_constructor_sets_output_mode() {
  IT("the constructor configures the enable pin as an output");
  resetGpioState();
  const ExternalSensor sensor(EN_PIN);
  IS_EQUAL(getPinMode(EN_PIN), OUTPUT);
  END_IT
}

bool test_on_drives_pin_high() {
  IT("on() drives the enable pin high");
  resetGpioState();
  const ExternalSensor sensor(EN_PIN);
  sensor.on();
  IS_EQUAL(getDigitalWriteValue(EN_PIN), HIGH);
  IS_TRUE(sensor.getState());
  END_IT
}

bool test_off_drives_pin_low() {
  IT("off() drives the enable pin low");
  resetGpioState();
  const ExternalSensor sensor(EN_PIN);
  sensor.on();
  sensor.off();
  IS_EQUAL(getDigitalWriteValue(EN_PIN), LOW);
  IS_FALSE(sensor.getState());
  END_IT
}

int main() {
  SUITE("ExternalSensor");
  test_constructor_sets_output_mode();
  test_on_drives_pin_high();
  test_off_drives_pin_low();
  FINISH
}
