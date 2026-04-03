#pragma once
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include <Wire.h>                                                   /// Library for I2C communication.
#include <ADS1X15.h>                                                /// Library for ADS1115 ADC.

/// @brief Handles ADC reading and MQTT message generation for analog input channels.
class AdcReader final : public MqttBase {
private:
  static constexpr uint8_t analogChannels = 4U;                     // Number of analog channels available on the ADC.
  static constexpr uint8_t maxChannelNumber = analogChannels - 1U;  // Maximum channel index (0-3).
  static constexpr uint8_t dataOutBufSize = 80U;                    // Maximum size of the MQTT message buffer.

  // MQTT message JSON format.
  static constexpr const char PROGMEM mqttMsgFrame[] = R"({"Analog":[%hd,%hd,%hd,%hd],"Voltage":[%.2f,%.2f,%.2f,%.2f]})";

public:
  /// @brief Enumeration representing ADC channels.
  enum class Channel : uint8_t {
    AN0 = 0U,
    AN1,
    AN2,
    AN3
  };

  /// @brief Constructs an AdcReader object.
  /// @param connectivity Reference to the MQTT connectivity object.
  /// @param subTopic MQTT sub topic for this object.
  /// @param measureTime Delay between ADC measurements in milliseconds.
  /// @param rdyPin GPIO pin used as the ALERT/RDY signal from the ADC.
  /// @param sdaPin SDA pin for I2C communication. Defaults to `SDA`.
  /// @param sclPin SCL pin for I2C communication. Defaults to `SCL`.
  /// @param address I2C address of the ADS1115 ADC. Defaults to `0x48`.
  AdcReader(Connectivity& connectivity, const char* subTopic, uint16_t measureTime, uint8_t rdyPin, uint8_t sdaPin = SDA, uint8_t sclPin = SCL, uint8_t address = 0x48);

  /// @brief Destructor of the object.
  ~AdcReader() override = default;

  /// @brief Initializes the ADC and configures necessary settings.
  /// @return `true` if initialization was successful, `false` otherwise.
  bool init() override;

  /// @brief Stops the ADC and releases resources.
  void end();

  /// @brief Main loop method for handling ADC state transitions and MQTT message sending.
  /// @return `true` if the ADC and MQTT operations were successful, `false` otherwise.
  bool run() override;

  /// @brief Handles incoming MQTT messages. Not used in this implementation.
  /// @param payloadJson JSON document containing the received MQTT message.
  void messageArrivedCallback(JsonDocument& payloadJson) override { // NOLINT(readability-convert-member-functions-to-static)
    (void)payloadJson;
  }

  /// @brief Reads the raw ADC value for a given channel.
  /// @param channel The ADC channel to read.
  /// @return The raw ADC value.
  int16_t analogRead(Channel channel);

  /// @brief Converts the raw ADC value to voltage for a given channel.
  /// @param channel The ADC channel to read.
  /// @return The voltage value corresponding to the ADC reading.
  float voltageRead(Channel channel);

  /// @brief Enables periodic MQTT message sending.
  /// @param interval Interval in milliseconds between MQTT messages.
  void enableMqttSending(uint32_t interval);

  /// @brief Disables periodic MQTT message sending.
  void disableMqttSending();

  /// @brief Checks if ADC values are ready for reading.
  /// @return `true` if values are ready, `false` otherwise.
  bool readyToRead() const;

  AdcReader(const AdcReader&) = delete;                       // Define copy constructor.
  AdcReader& operator=(const AdcReader&) = delete;            // Define copy assignment operator.
  AdcReader(AdcReader&&) = delete;                            // Define move constructor.
  AdcReader& operator=(AdcReader&&) = delete;                 // Define move assignment operator.

private:
  /// @brief States representing the measurement process.
  enum class MeasureStates : uint8_t {
    IDLE = 0U,                      // Waiting for ADC readiness.
    REQUEST_ADC,                    // Requesting ADC measurement.
    STORE_DATA,                     // Storing the measured ADC value.
    MEASURE_DELAY                   // Waiting before starting the next measurement.
  };

  /// @brief Interrupt handler for the ALERT/RDY pin.
  static IRAM_ATTR void intHandler();

  static volatile bool adcReady;                              // Flag indicating ADC readiness (interrupt-driven).

  ADS1115 ADS;                                                // ADS1115 ADC object.
  const uint16_t measureTime;                                 // Time between measurements in milliseconds.
  const uint8_t rdyPin;                                       // GPIO pin for the ALERT/RDY signal.
  uint8_t channel;                                            // Current ADC channel being read.
  int16_t adcValues[analogChannels];                          // Array storing raw ADC values for each channel.
  MeasureStates measureState;                                 // Current state of the measurement process.
  uint32_t measureTimer;                                      // Timer for managing measurement delays.
  bool enableSending;                                         // Flag indicating if MQTT message sending is enabled.
  uint32_t mqttSendTime;                                      // Interval for sending MQTT messages.
  uint32_t mqttSendTimer;                                     // Timer for managing MQTT message sending.
  const uint32_t adsReadWdTime;                               // Watchdog timer duration for ADC read operations.
  uint32_t adsReadWdTimer;                                    // Timer for the ADC read watchdog.
  bool valuesReady;                                           // Flag indicating if ADC values are ready for use.
};