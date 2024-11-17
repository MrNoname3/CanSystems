#ifndef MOISTURE_READER_HPP
#define MOISTURE_READER_HPP

#include "stdint.h"                                                 /// Standard fixed-width integer types.
#include "multiplexer.hpp"                                          /// Analog multiplexer class.
#include "taskRunner.hpp"                                           /// Task runner class.
#include "common.hpp"                                               /// Common definitions and functions.
#include "rgbLedWrapper.hpp"                                        /// RGB LED driver wrapper.

template<uint8_t N>
class MoistureReader final : public TaskRunner {
public:
  MoistureReader(const Multiplexer& multiplexer, RgbLedWrapper& rgbLed, const uint8_t (&channels)[N], uint32_t readTime, void (*dataSender)(const uint8_t (&data)[8]));
  ~MoistureReader() = default;

  virtual void init() override;
  virtual void run() override;
  void triggerImmediateMeasurement();

  // Delete copy and move constructors/assignment operators
  MoistureReader(const MoistureReader&) = delete;
  MoistureReader& operator=(const MoistureReader&) = delete;
  MoistureReader(MoistureReader&&) = delete;
  MoistureReader& operator=(MoistureReader&&) = delete;

private:
  enum class ReadState : uint8_t {
    IDLE = 0U,
    WAKEUP,
    SETUP,
    READING
  };

  void filterAnalogValue();

  static constexpr uint8_t channelNum = N;
  const Multiplexer& multiplexer;
  RgbLedWrapper& rgbLed;                                                    // Reference to RGB LED driver object.
  const uint8_t (&channels)[N];
  const uint32_t readTime;
  uint32_t eventTimer;
  void (*dataSender)(const uint8_t (&data)[8]);
  static constexpr uint32_t sensorWakeupTime = TimeConverter::secToMs(10U);         // 10 seconds.
  ReadState readState;
  uint8_t readIndex;
  static constexpr uint32_t filteringTime = TimeConverter::secToMs(2U);             // 2 seconds.
  uint16_t moistureValue;
  static constexpr uint8_t readStartColors[3] = {5U, 3U, 0U};               // RGB LED colors when reading started.
  static constexpr uint32_t readTimeOffset = sensorWakeupTime + channelNum * filteringTime;
};

template<uint8_t N>
MoistureReader<N>::MoistureReader(const Multiplexer& multiplexer, RgbLedWrapper& rgbLed, const uint8_t (&channels)[N], uint32_t readTime, void (*dataSender)(const uint8_t (&data)[8])) :
  multiplexer(multiplexer),
  rgbLed(rgbLed),
  channels(channels),
  readTime(readTime),
  eventTimer(0UL),
  dataSender(dataSender),
  readState(ReadState::IDLE),
  readIndex(0U),
  moistureValue(0U)
{}

template<uint8_t N>
void MoistureReader<N>::init() {
  eventTimer = millis();
}

template<uint8_t N>
void MoistureReader<N>::run() {
  const uint32_t actualTime = millis();
  switch(readState) {
    case ReadState::IDLE: {
      if(actualTime - eventTimer > readTime) {
        eventTimer = actualTime;
        multiplexer.enableRead();
        rgbLed.setColor(readStartColors[0], readStartColors[1], readStartColors[2], false);
        readState = ReadState::WAKEUP;
      }
    } break;
    case ReadState::WAKEUP: {
      if(actualTime - eventTimer > sensorWakeupTime) {
        readState = ReadState::SETUP;
      }
    } break;
    case ReadState::SETUP: {
        multiplexer.selectChannel(channels[readIndex]);
        eventTimer = actualTime;
        moistureValue = 0U;
        readState = ReadState::READING;
    } break;
    case ReadState::READING: {
      filterAnalogValue();
      if(actualTime - eventTimer > filteringTime) {
        if(dataSender != nullptr) {
          const uint8_t moistureH = static_cast<uint8_t>((moistureValue >> 0) & 0xFF);
          const uint8_t moistureL = static_cast<uint8_t>((moistureValue >> 8) & 0xFF);
          dataSender({channels[readIndex], moistureH, moistureL, 0U, 0U, 0U, 0U, 0U});
        }
        readIndex++;
        if(readIndex >= channelNum) {
          readIndex = 0U;
          multiplexer.disableRead();
          rgbLed.clear();
          eventTimer = actualTime - readTimeOffset;
          readState = ReadState::IDLE;
        } else {
          readState = ReadState::SETUP;
        }
      }
    } break;
  };
}

template<uint8_t N>
void MoistureReader<N>::triggerImmediateMeasurement() {
  if(readState == ReadState::IDLE) {
    eventTimer = millis() - readTime;
  }
}

template<uint8_t N>
void MoistureReader<N>::filterAnalogValue() {
  // Complement filter calculation.
  static constexpr uint8_t adcInputFilterAlpha = 10U;     // Complement filter ALPHA value.
  const uint16_t rawAnalogValue = multiplexer.analogReadAdvanced();
  moistureValue = ((adcInputFilterAlpha * rawAnalogValue) + (100U - adcInputFilterAlpha) * (uint32_t)moistureValue) / 100U;
}
#endif // MOISTURE_READER_HPP