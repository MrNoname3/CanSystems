#ifndef AMBIENT_SENSOR_HPP
#define AMBIENT_SENSOR_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "canHandler.hpp"                                           /// CAN handler library.
#include <SI7021.h>                                                 /// Temperature and humidity sensor driver.
#include <HardwareSerial.h>                                         /// Arduino hardware serial lib.
#include "taskHandler.hpp"                                          /// Class for task scheduling.

/// @brief Manages ambient measurements including temperature, humidity, and light intensity.
/// @details Uses an SI7021 sensor for temperature and humidity readings, an analog pin for light intensity,
/// and communicates data via CAN and serial output.
class AmbientSensor final : public Task {
public:
  /// @brief Constructor for the AmbientSensor class.
  /// @param canHandler CAN handler instance for communication.
  /// @param lightPin Analog pin for reading light intensity.
  /// @param measurePeriod Time interval for sensor measurements in milliseconds.
  AmbientSensor(CanHandler& canHandler, uint8_t lightPin, uint32_t measurePeriod);

  /// @brief Destructor for the AmbientSensor object.
  ~AmbientSensor() = default;

  /// @brief Initializes the sensor and communication interfaces.
  /// @return `true` if the execution was successfully, `false` otherwise.
  virtual bool init() override;

  /// @brief Periodically reads sensors and sends data via CAN and serial.
  virtual void run() override;

  AmbientSensor(const AmbientSensor&) = delete;                       // Define copy constructor.
  AmbientSensor& operator=(const AmbientSensor&) = delete;            // Define copy assignment operator.
  AmbientSensor(AmbientSensor&&) = delete;                            // Define move constructor.
  AmbientSensor& operator=(AmbientSensor&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Defines the state of the sensor task.
  enum class Event : uint8_t {
    IDLE = 0U,                        // Idle state, waiting for the next action.
    READ_TEMPERATURE,                 // Read temperature from the SI7021 sensor.
    READ_HUMIDITY,                    // Read humidity from the SI7021 sensor.
    SEND_VALUES                       // Transmit the collected data.
  };

  static constexpr uint32_t clockSpeed = 100000U;                           // Default I2C clock speed in Hz.

  SI7021 si7021;                                                            // I2C humidity and temperature sensor driver.
  CanHandler& canHandler;                                                   // Reference to a CAN handler object.
  const uint8_t lightPin;                                                   // Analog pin for light intensity readings.
  const uint32_t measurePeriod;                                             // Measurement interval in milliseconds.
  uint16_t lightValue;                                                      // Filtered light intensity value.
  int16_t temperature;                                                      // Current temperature reading in hundredths of a degree Celsius.
  uint16_t humidity;                                                        // Current relative humidity reading in percentage.
  uint32_t eventTimer;                                                      // Class wide variable for universal timings.
  Event event;                                                              // Current state of the sensor task.
};
#endif // AMBIENT_SENSOR_HPP