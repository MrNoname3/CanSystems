#ifndef AMBIENT_SENSOR_HPP
#define AMBIENT_SENSOR_HPP

#include <stdint.h>
#include "canHandler/src/canHandler.hpp"
#include <SI7021.h>                                                 /// Temperature and humidity sensor driver.
#include <HardwareSerial.h>

class AmbientSensor final {
public:
  AmbientSensor(HardwareSerial& serial, CanHandler& canHandler, uint8_t lightPin, uint32_t measurePeriod);
  /// @brief Destructor of the object.
  ~AmbientSensor() = default;
  void begin();
  void loop();

  AmbientSensor(const AmbientSensor&) = delete;                       // Define copy constructor.
  AmbientSensor& operator=(const AmbientSensor&) = delete;            // Define copy assignment operator.
  AmbientSensor(AmbientSensor&&) = delete;                            // Define move constructor.
  AmbientSensor& operator=(AmbientSensor&&) = delete;                 // Define move assignment operator
private:
  SI7021 si7021;                                                      // I2C humidity and temperature sensor driver.
  HardwareSerial& serialPort;
  CanHandler& canHandler;
  const uint8_t lightPin;
  const uint32_t measurePeriod;
};
#endif // AMBIENT_SENSOR_HPP