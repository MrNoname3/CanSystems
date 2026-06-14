#ifndef AMBIENT_SENSOR_HPP
#define AMBIENT_SENSOR_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "canHandler.hpp"                                           /// CAN handler library.
#include "si7021.hpp"                                               /// Temperature and humidity sensor driver.
#include "taskHandler.hpp"                                          /// Class for task scheduling.

/// @brief Manages ambient measurements including temperature, humidity, and light intensity.
/// @details Uses an SI7021 sensor for temperature and humidity readings, an analog pin for light intensity,
/// and communicates data via CAN.
class AmbientSensor final : public Task {
public:
  /// @brief Constructor for the AmbientSensor class.
  /// @param canHandler CAN handler instance for communication.
  /// @param lightPin Analog pin for reading light intensity.
  /// @param measurePeriod Time interval for sensor measurements in milliseconds.
  AmbientSensor(CanHandler& canHandler, uint8_t lightPin, uint32_t measurePeriod);

  /// @brief Destructor for the AmbientSensor object.
  ~AmbientSensor() override = default;

  /// @brief Initializes the sensor and communication interfaces.
  /// @return `true` if the execution was successfully, `false` otherwise.
  bool init() override;

  /// @brief Periodically reads sensors and sends data via CAN.
  /// @return `true`.
  bool run() override;

  AmbientSensor(const AmbientSensor&) = delete;                       // Define copy constructor.
  AmbientSensor& operator=(const AmbientSensor&) = delete;            // Define copy assignment operator.
  AmbientSensor(AmbientSensor&&) = delete;                            // Define move constructor.
  AmbientSensor& operator=(AmbientSensor&&) = delete;                 // Define move assignment operator.

private:
  static constexpr uint32_t kSendMaxPeriod = 1800000UL;  // 30 minutes between forced sends.
  static constexpr int16_t kTempTolerance = 50;          // 0.50 °C in hundredths of a degree.
  static constexpr uint8_t kHumTolerance = 3U;           // 3 % relative humidity.
  static constexpr uint16_t kLightTolerance = 20U;       // Filtered ADC counts.

  /// @brief Defines the state of the sensor task.
  enum class Event : uint8_t {
    IDLE = 0U,                        // Idle state, waiting for the next action.
    READ_TEMPERATURE,                 // Read temperature from the SI7021 sensor.
    READ_HUMIDITY,                    // Read humidity from the SI7021 sensor.
    CHECK_SEND,                       // Decide whether to send based on threshold or elapsed time.
    SEND_VALUES,                      // Transmit the collected data.
    SENSOR_ERROR                      // Handle sensor errors.
  };

  SI7021 si7021;                                                            // I2C humidity and temperature sensor driver.
  CanHandler& canHandler;                                                   // Reference to a CAN handler object.
  const uint8_t lightPin;                                                   // Analog pin for light intensity readings.
  const uint32_t measurePeriod;                                             // Measurement interval in milliseconds.
  uint16_t lightValue;                                                      // Filtered light intensity value.
  int16_t temperature;                                                      // Current temperature reading in hundredths of a degree Celsius.
  uint16_t humidity;                                                        // Current relative humidity reading in percentage.
  int16_t lastSentTemperature;                                              // Temperature value at the last send (sentinel INT16_MIN forces first send).
  uint16_t lastSentHumidity;                                                // Humidity value at the last send (sentinel UINT16_MAX forces first send).
  uint16_t lastSentLight;                                                   // Light value at the last send (sentinel UINT16_MAX forces first send).
  uint32_t eventTimer;                                                      // Timer for measurement period.
  uint32_t sendThrottleTimer;                                               // Timer for the 30-minute forced-send period.
  Event event;                                                              // Current state of the sensor task.
};
#endif // AMBIENT_SENSOR_HPP