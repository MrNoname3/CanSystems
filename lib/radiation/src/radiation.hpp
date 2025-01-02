#ifndef RADIATION_HPP
#define RADIATION_HPP

#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include <Arduino.h>                                                /// Arduino libraries header.
#include <Ticker.h>                                                 /// Timer interrupt handler.
#include "common.hpp"                                               /// Common definitions and functions.

/// @brief Class for monitoring radiation levels using a sensor and publishing data over MQTT.
/// The class counts radiation pulses detected on a sensor pin, calculates counts per minute (CPM),
/// and sends the data as an MQTT message at regular intervals.
class Radiation final : public MqttBase {
private:
  static constexpr uint8_t dataOutBufSize = 16U;              // Size of the buffer used for outgoing MQTT data messages.
  static constexpr uint32_t measureTime = Time::minToMs(1U);  // Measurement interval in milliseconds for calculating CPM.

public:
  /// @brief Constructs the Radiation monitoring object.
  /// @param connectivity Reference to the MQTT connectivity object.
  /// @param subtopic Subtopic for MQTT messages related to radiation data.
  /// @param sensorPin GPIO pin connected to the radiation sensor.
  Radiation(Connectivity& connectivity, const char* subtopic, uint8_t sensorPin);

  /// @brief Destructor of the object.
  ~Radiation() = default;

  /// @brief Initializes the radiation sensor and measurement system.
  /// @return `true` if initialization was successful; otherwise, `false`.
  virtual bool init() override;

  /// @brief Executes the radiation monitoring logic and sends data if ready.
  /// @return `true` if the operation was successful; otherwise, `false`.
  virtual bool run() override;

  /// @brief Stops the radiation monitoring and detaches interrupts.
  void end();

  /// @brief Callback function invoked when an MQTT message is received.
  /// This implementation does not handle incoming messages.
  /// @param payload Pointer to the received message payload.
  /// @param length Length of the payload in bytes.
  virtual void messageArrivedCallback(const uint8_t* payload, uint32_t length) override {
    (void)payload;
    (void)length;
  }

  Radiation(const Radiation&) = delete;                       // Define copy constructor.
  Radiation& operator=(const Radiation&) = delete;            // Define copy assignment operator.
  Radiation(Radiation&&) = delete;                            // Define move constructor.
  Radiation& operator=(Radiation&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Interrupt Service Routine (ISR) for counting radiation pulses.
  static IRAM_ATTR void counter();

  /// @brief ISR for measuring CPM and preparing data for transmission.
  static IRAM_ATTR void measure();

  static const char PROGMEM cpmMessageFrame[];  // Format string for the MQTT message containing CPM data.
  static volatile uint16_t cpm;                 // Counter for radiation pulses detected during the current measurement period.
  static volatile bool measureDone;             // Flag indicating whether a measurement period has completed.
  static volatile uint16_t cpmToSend;           // CPM value to be sent over MQTT.

  Ticker measureTicker;                         // Timer used for periodically triggering the measurement ISR.
  const uint8_t sensorPin;                      // GPIO pin connected to the radiation sensor.
};
#endif // RADIATION_HPP