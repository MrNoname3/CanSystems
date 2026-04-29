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
public:
  /// @brief Supported Geiger-Müller tube types.
  /// Values must match the integer stored in /config/tube.json: { "tube": <value> }.
  enum class TubeType : uint8_t { Unknown = 0U, J305 = 1U, M4011 = 2U };

private:
  static constexpr uint8_t  dataOutBufSize = 64U;              // Buffer for outgoing MQTT data messages.
  static constexpr uint32_t measureTime = Time::minToMs(1U);   // Measurement interval in milliseconds for calculating CPM.

  // CPM-only message frame (used when tube type is unknown).
  static constexpr const char PROGMEM cpmMessageFrame[]  = R"({"cpm":%hu})";
  // Full message frame including sievert (µSv/h) and radian (µrad/h) computed from CPM.
  // Values use fixed-point integer formatting to avoid float printf on ESP8266.
  static constexpr const char PROGMEM fullMessageFrame[] = R"({"cpm":%hu,"sievert":%u.%04u,"radian":%u.%02u})";

public:
  /// @brief Constructs the Radiation monitoring object.
  /// @param connectivity Reference to the MQTT connectivity object.
  /// @param subtopic Subtopic for MQTT messages related to radiation data.
  /// @param sensorPin GPIO pin connected to the radiation sensor.
  Radiation(Connectivity& connectivity, const char* subtopic, uint8_t sensorPin);

  /// @brief Destructor of the object.
  ~Radiation() override = default;

  /// @brief Initializes the radiation sensor and measurement system.
  /// @return `true` if initialization was successful; otherwise, `false`.
  bool init() override;

  /// @brief Executes the radiation monitoring logic and sends data if ready.
  /// @return `true` if the operation was successful; otherwise, `false`.
  bool run() override;

  /// @brief Publishes the HA MQTT discovery config for the radiation sensor entity.
  /// @return `true` if publishing succeeded; otherwise, `false`.
  bool publishDiscovery() override;

  /// @brief Stops the radiation monitoring and detaches interrupts.
  void end();

  /// @brief Callback invoked when an MQTT message arrives, with the payload already parsed into a JSON document.
  /// Derived classes must implement this to handle incoming messages.
  /// @param payloadJson Reference to a `JsonDocument` containing the parsed payload of the incoming message.
  void messageArrivedCallback(JsonDocument& payloadJson) override { // NOLINT(readability-convert-member-functions-to-static)
    (void)payloadJson;
  }

  Radiation(const Radiation&) = delete;                       // Define copy constructor.
  Radiation& operator=(const Radiation&) = delete;            // Define copy assignment operator.
  Radiation(Radiation&&) = delete;                            // Define move constructor.
  Radiation& operator=(Radiation&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Reads the tube type from /config/tube.json and returns the matching enum value.
  static TubeType loadTubeType();

  /// @brief Returns the CPM-to-µSv/h conversion factor for the given tube type; 0.0f if unknown.
  static constexpr float getTubeFactor(TubeType t) {
    switch(t) {
      case TubeType::J305:  return 123.153f;
      case TubeType::M4011: return 153.8f;
      default:              return 0.0f;
    }
  }

  /// @brief Interrupt Service Routine (ISR) for counting radiation pulses.
  static IRAM_ATTR void counter();

  /// @brief ISR for measuring CPM and preparing data for transmission.
  static IRAM_ATTR void measure();

  static volatile uint16_t cpm;                 // Counter for radiation pulses detected during the current measurement period.
  static volatile bool measureDone;             // Flag indicating whether a measurement period has completed.
  static volatile uint16_t cpmToSend;           // CPM value to be sent over MQTT.

  Ticker measureTicker;                         // Timer used for periodically triggering the measurement ISR.
  const uint8_t sensorPin;                      // GPIO pin connected to the radiation sensor.
  TubeType tubeType = TubeType::Unknown;        // Tube type read from config at init; determines sievert/radian calculation.
};
#endif // RADIATION_HPP