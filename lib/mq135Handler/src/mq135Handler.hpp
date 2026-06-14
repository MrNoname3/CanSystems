#ifndef MQ135_HANDLER_HPP
#define MQ135_HANDLER_HPP

#include "connectivity.hpp"
#include "adcReader.hpp"
#include <MQUnifiedsensor.h>

class Mq135Handler final : public MqttBase {
private:
  static constexpr uint8_t dataOutBufSize = 192U;

  static constexpr const char PROGMEM mqttMsgFrame[] = R"({"Gas":{"CO":%.2f,"Alcohol":%.2f,"CO2":%.2f,"Toluene":%.2f,"NH4":%.2f,"Acetone":%.2f}})";

public:
  Mq135Handler(Connectivity& connectivity, const char* classID, AdcReader& adcReader, AdcReader::Channel channel, uint32_t measureTime);

  /// @brief Destructor of the object.
  ~Mq135Handler() override = default;

  bool init() override;

  bool run() override;

  void messageArrivedCallback(JsonDocument& payloadJson) override { // NOLINT(readability-convert-member-functions-to-static)
    (void)payloadJson;
  }

  bool startCalibration();

  Mq135Handler(const Mq135Handler&) = delete;                       // Define copy constructor.
  Mq135Handler& operator=(const Mq135Handler&) = delete;            // Define copy assignment operator.
  Mq135Handler(Mq135Handler&&) = delete;                            // Define move constructor.
  Mq135Handler& operator=(Mq135Handler&&) = delete;                 // Define move assignment operator.

private:
  // Gas sensor calibration values:
  static constexpr float sensorVoltage = 5.0F;
  static constexpr uint8_t adcResolution = 12U;
  static constexpr uint8_t adcPin = 255U;
  static constexpr float ratioMQ135CleanAir = 3.6F;      // RS/R0 = 3.6 ppm.
  static constexpr float rlValue = 1.0F;
  static constexpr float r0Value = 22.47F;

  // Exponential regression:
  static constexpr float gasEquationValues[][2] = {
    { 605.18F, -3.937F },  // CO
    { 77.255F, -3.180F },  // Alcohol
    { 110.47F, -2.862F },  // CO2
    { 44.947F, -3.445F },  // Toluene
    { 102.20F, -2.473F },  // NH4
    { 34.668F, -3.369F }   // Acetone
  };

  // Number of rows (number of gases).
  static constexpr uint8_t numGases = sizeof(gasEquationValues) / sizeof(gasEquationValues[0]);

  static constexpr float gasReadOffset[numGases] = {
    0.1F,   // CO
    0.0F,   // Alcohol
    400.0F, // CO2
    0.0F,   // Toluene
    0.0F,   // NH4
    0.0F    // Acetone
  };

  enum class EQ : uint8_t {
    A = 0,
    B
  };

  enum class GasReadState : uint8_t {
    IDLE = 0,
    CALIBRATION,
    READ,
    SEND
  };

  uint16_t getAnalogValue();

  AdcReader& adcReader;
  const AdcReader::Channel channel;
  MQUnifiedsensor mq135;
  const uint32_t measureTime;
  uint32_t measureTimer;
  GasReadState gasReadState;
  uint8_t readIndex;
  float gasValues[numGases];
};
#endif // MQ135_HANDLER_HPP