#ifndef ADC_READER_HPP
#define ADC_READER_HPP

#include "connectivity.hpp"
#include <Wire.h>
#include <ADS1X15.h>

class AdcReader final : public MqttBase {
private:
  static constexpr uint8_t analogChannels = 4U;
  static constexpr uint8_t maxChannelNumber = analogChannels - 1U;   // Channels: 0-3.
  static constexpr uint8_t dataOutBufSize = 128U;

  static inline const char PROGMEM mqttMsgFrame[] = {
    "{"
      "\"Analog\":[%hd,%hd,%hd,%hd],"
      "\"Voltage\":[%.2f,%.2f,%.2f,%.2f]"
    "}"
  };

public:
  enum class Channel : uint8_t {
    AN0 = 0U,
    AN1,
    AN2,
    AN3
  };

  AdcReader(Connectivity& connectivity, const char* classID, uint16_t measureTime, uint8_t rdyPin, uint8_t sdaPin = SDA, uint8_t sclPin = SCL, uint8_t address = 0x48);

  /// @brief Destructor of the object.
  virtual ~AdcReader() = default;

  virtual bool init() override;

  void end();

  virtual bool run() override;

  virtual void messageArrivedCallback(JsonDocument& payloadJson) override {
    (void)payloadJson;
  }

  int16_t analogRead(Channel channel);

  float voltageRead(Channel channel);

  void enableMqttSending(uint32_t interval);

  void disableMqttSending();

  bool readyToRead();

  AdcReader(const AdcReader&) = delete;                       // Define copy constructor.
  AdcReader& operator=(const AdcReader&) = delete;            // Define copy assignment operator.
  AdcReader(AdcReader&&) = delete;                            // Define move constructor.
  AdcReader& operator=(AdcReader&&) = delete;                 // Define move assignment operator.

private:
  enum class MeasureStates : uint8_t {
    IDLE = 0U,
    REQUEST_ADC,
    STORE_DATA,
    MEASURE_DELAY
  };

  static IRAM_ATTR void intHandler();

  static volatile bool adcReady;

  ADS1115 ADS;
  const uint16_t measureTime;
  const uint8_t rdyPin;
  uint8_t channel;
  int16_t adcValues[analogChannels];
  MeasureStates measureState;
  uint32_t measureTimer;
  bool enableSending;
  uint32_t mqttSendTime;
  uint32_t mqttSendTimer;
  const uint32_t adsReadWdTime;
  uint32_t adsReadWdTimer;
  bool valuesReady;
};
#endif // ADC_READER_HPP