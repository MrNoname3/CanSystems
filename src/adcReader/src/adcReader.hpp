#ifndef ADC_READER_HPP
#define ADC_READER_HPP

#include "../../connectivity/src/connectivity.hpp"
#include <Wire.h>
#include <ADS1X15.h>

class AdcReader final : public Connectivity::MqttComBase {
public:
  enum class Channel : uint8_t {
    AN0 = 0,
    AN1,
    AN2,
    AN3
  };

  AdcReader(Connectivity& connectivity, const char* classID, uint16_t measureTime, uint8_t rdyPin, uint8_t address = 0x48);

  /// @brief Destructor of the object.
  virtual ~AdcReader() = default;

  virtual bool begin() override;

  void end();

  virtual bool loop() override;

  virtual void messageReceived(uint8_t* payload, uint32_t length) override;

  int16_t analogRead(Channel channel);

  float voltageRead(Channel channel);

  void enableMqttSending(uint32_t interval);

  void disableMqttSending();

  AdcReader(const AdcReader&) = delete;                       // Define copy constructor.
  AdcReader& operator=(const AdcReader&) = delete;            // Define copy assignment operator.
  AdcReader(AdcReader&&) = delete;                            // Define move constructor.
  AdcReader& operator=(AdcReader&&) = delete;                 // Define move assignment operator.

private:
  enum class MeasureStates : uint8_t {
    IDLE = 0,
    REQUEST_ADC,
    STORE_DATA,
    MEASURE_DELAY
  };

  inline static IRAM_ATTR void intHandler();

  static constexpr uint8_t analogChannels = 4;
  ADS1115 ADS;
  const uint16_t measureTime;
  const uint8_t rdyPin;
  uint8_t channel;
  int16_t adcValues[analogChannels];
  static volatile bool adcReady;
  MeasureStates measureState;
  uint32_t measureTimer;
  bool enableSending;
  uint32_t mqttSendTime;
  uint32_t mqttSendTimer;
  const uint32_t adsReadWdTime;
  uint32_t adsReadWdTimer;
  static constexpr uint8_t maxChannelNumber = analogChannels - 1;   // Channels: 0-3.
  static constexpr uint8_t dataOutBufSize = 128;
  static const char PROGMEM MQTT_MSG_FRAME[];
};
#endif // ADC_READER_HPP