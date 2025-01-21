#ifndef MQ135_HANDLER_HPP
#define MQ135_HANDLER_HPP

#include "connectivity.hpp"
#include "adcReader.hpp"
#include <MQUnifiedsensor.h>

class Mq135Handler final : public MqttBase {
private:
  static constexpr uint8_t dataOutBufSize = 192U;

  static inline const char PROGMEM mqttMsgFrame[] = R"({"Gas":{"CO":%.2f,"Alcohol":%.2f,"CO2":%.2f,"Toluene":%.2f,"NH4":%.2f,"Acetone":%.2f}})";

public:
  Mq135Handler(Connectivity& connectivity, const char* classID, AdcReader& adcReader, AdcReader::Channel channel, uint32_t measureTime);

  /// @brief Destructor of the object.
  virtual ~Mq135Handler() = default;

  virtual bool init() override;

  virtual bool run() override;

  virtual void messageArrivedCallback(JsonDocument& payloadJson) override {
    (void)payloadJson;
  }

  bool startCalibration();

  Mq135Handler(const Mq135Handler&) = delete;                       // Define copy constructor.
  Mq135Handler& operator=(const Mq135Handler&) = delete;            // Define copy assignment operator.
  Mq135Handler(Mq135Handler&&) = delete;                            // Define move constructor.
  Mq135Handler& operator=(Mq135Handler&&) = delete;                 // Define move assignment operator.

private:
  // Gas sensor calibration values:
  static constexpr float sensorVoltage = 5.0f;
  static constexpr uint8_t adcResolution = 12U;
  static constexpr uint8_t adcPin = -1;
  static constexpr float ratioMQ135CleanAir =  3.6f;      // RS/R0 = 3.6 ppm.
  static constexpr float rlValue = 1.0f;
  static constexpr float r0Value = 22.47f;

  // Exponential regression:
  static constexpr float gasEquationValues[][2] = {
    {605.18f, -3.937f},  // CO
    {77.255f, -3.180f},  // Alcohol
    {110.47f, -2.862f},  // CO2
    {44.947f, -3.445f},  // Toluene
    {102.20f, -2.473f},  // NH4
    {34.668f, -3.369f}   // Acetone
  };

  // Number of rows (number of gases).
  static constexpr uint8_t numGases = sizeof(gasEquationValues) / sizeof(gasEquationValues[0]);

  static constexpr float gasReadOffset[numGases] = {
    0.1f,   // CO
    0.0f,   // Alcohol
    400.0f, // CO2
    0.0f,   // Toluene
    0.0f,   // NH4
    0.0f    // Acetone
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