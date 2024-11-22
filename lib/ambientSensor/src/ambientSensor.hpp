#ifndef AMBIENT_SENSOR_HPP
#define AMBIENT_SENSOR_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "canHandler.hpp"                                           /// CAN handler library.
#include <SI7021.h>                                                 /// Temperature and humidity sensor driver.
#include <HardwareSerial.h>                                         /// Arduino hardware serial lib.
#include "taskRunner.hpp"                                           /// Task runner class.

/// @brief Manages ambient measurements including temperature, humidity, and light intensity.
/// @details Uses an SI7021 sensor for temperature and humidity readings, an analog pin for light intensity,
/// and communicates data via CAN and serial output.
class AmbientSensor final : public TaskRunner {
public:
  /// @brief Constructor for the AmbientSensor class.
  /// @param serial HardwareSerial instance for serial communication.
  /// @param canHandler CAN handler instance for communication.
  /// @param lightPin Analog pin for reading light intensity.
  /// @param measurePeriod Time interval for sensor measurements in milliseconds.
  AmbientSensor(HardwareSerial& serial, CanHandler& canHandler, uint8_t lightPin, uint32_t measurePeriod);

  /// @brief Destructor for the AmbientSensor object.
  ~AmbientSensor() = default;

  /// @brief Initializes the sensor and communication interfaces.
  virtual void init() override;

  /// @brief Periodically reads sensors and sends data via CAN and serial.
  virtual void run() override;

  AmbientSensor(const AmbientSensor&) = delete;                       // Define copy constructor.
  AmbientSensor& operator=(const AmbientSensor&) = delete;            // Define copy assignment operator.
  AmbientSensor(AmbientSensor&&) = delete;                            // Define move constructor.
  AmbientSensor& operator=(AmbientSensor&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Filters the analog value of the current sensor.
  void filterAnalogValue();

  SI7021 si7021;                                                            // I2C humidity and temperature sensor driver.
  HardwareSerial& serialPort;                                               // Reference to a HardwareSerial object.
  CanHandler& canHandler;                                                   // Reference to a CAN handler object.
  const uint8_t lightPin;                                                   // Analog pin for light intensity readings.
  const uint32_t measurePeriod;                                             // Measurement interval in milliseconds.
  uint16_t lightValue;                                                      // Filtered light intensity value.
  uint32_t eventTimer;                                                      // Class wide variable for universal timings.
};
#endif // AMBIENT_SENSOR_HPP