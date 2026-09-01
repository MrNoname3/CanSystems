#pragma once

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Manages an external sensor by controlling its enable pin.
/// @details on() and off() are const: the pin they drive is the sensor's, not this object's
/// state, so a const instance can still switch it.
class ExternalSensor final {
public:
  /// @brief Constructs the ExternalSensor object.
  /// @param sensorEnablePin Digital pin used to enable or disable the external sensor.
  explicit ExternalSensor(uint8_t sensorEnablePin);

  /// @brief Default destructor.
  ~ExternalSensor() = default;

  /// @brief Turns on the external sensor by setting the enable pin to HIGH.
  void on() const;

  /// @brief Turns off the external sensor by setting the enable pin to LOW.
  void off() const;

  /// @brief Gets the current state of the sensor enable pin.
  /// @return True if the enable pin is HIGH, false otherwise.
  [[nodiscard]] bool getState() const;

  ExternalSensor(const ExternalSensor&) = delete;               // Define copy constructor.
  ExternalSensor& operator=(const ExternalSensor&) = delete;    // Define copy assignment operator.
  ExternalSensor(ExternalSensor&&) = delete;                    // Define move constructor.
  ExternalSensor& operator=(ExternalSensor&&) = delete;         // Define move assignment operator.

private:
  const uint8_t sensorEnPin;                          // Digital pin to control the external sensor.
};
