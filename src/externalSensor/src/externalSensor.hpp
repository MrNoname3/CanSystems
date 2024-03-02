#ifndef EXTERNAL_SENSOR_HPP
#define EXTERNAL_SENSOR_HPP

#include <stdint.h>

class ExternalSensor final {
public:
  ExternalSensor(const uint8_t sensorEnablePin);
  /// @brief Destructor of the object.
  ~ExternalSensor() = default;
  void on() const;
  void off() const;
  bool getState() const;
  ExternalSensor(const ExternalSensor&) = delete;               // Define copy constructor.
  ExternalSensor& operator=(const ExternalSensor&) = delete;    // Define copy assignment operator.
  ExternalSensor(ExternalSensor&&) = delete;                    // Define move constructor.
  ExternalSensor& operator=(ExternalSensor&&) = delete;         // Define move assignment operator.
private:
  const uint8_t sensorEnPin;
};
#endif // EXTERNAL_SENSOR_HPP