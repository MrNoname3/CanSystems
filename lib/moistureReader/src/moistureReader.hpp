#ifndef MOISTURE_READER_HPP
#define MOISTURE_READER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "multiplexer.hpp"                                          /// Analog multiplexer class.
#include "taskHandler.hpp"                                          /// Class for task scheduling.
#include "common.hpp"                                               /// Common definitions and functions.
#include "rgbLedWrapper.hpp"                                        /// RGB LED driver wrapper.

/// @brief Handles reading moisture sensor data through an analog multiplexer.
/// @tparam N Number of channels to read.
template<uint8_t N>
class MoistureReader final : public Task {
public:
  static_assert(N > 0U, "MoistureReader requires at least one channel!");

  /// @brief Constructor for the MoistureReader class.
  /// @param multiplexer Reference to the analog multiplexer object.
  /// @param rgbLed Reference to the RGB LED wrapper object.
  /// @param channels Array of multiplexer channels to read from.
  /// @param readTime Interval between consecutive readings in milliseconds.
  /// @param dataSender Function pointer to send sensor data.
  MoistureReader(const Multiplexer& multiplexer, RgbLedWrapper& rgbLed, const uint8_t (&channels)[N], uint32_t readTime, void (*dataSender)(const uint8_t (&data)[8]));

  /// @brief Default destructor.
  ~MoistureReader() override = default;

  /// @brief Initializes the MoistureReader, setting up its internal timer.
  /// @return `true`.
  bool init() override;

  /// @brief Executes the state machine to read moisture data.
  /// @return `true`.
  bool run() override;

  /// @brief Triggers an immediate measurement by resetting the timer.
  void triggerImmediateMeasurement();

  MoistureReader(const MoistureReader&) = delete;               // Define copy constructor.
  MoistureReader& operator=(const MoistureReader&) = delete;    // Define copy assignment operator.
  MoistureReader(MoistureReader&&) = delete;                    // Define move constructor.
  MoistureReader& operator=(MoistureReader&&) = delete;         // Define move assignment operator.

private:
  /// @brief State machine states for moisture reading.
  enum class ReadState : uint8_t {
    IDLE = 0U,                                // Waiting for the next read cycle.
    WAKEUP,                                   // Waiting for the sensor wake-up time.
    SETUP,                                    // Setting up the next multiplexer channel.
    READING                                   // Filtering and reading analog data.
  };

  /// @brief Applies a complementary filter to the analog moisture value.
  void filterAnalogValue();

  static constexpr uint8_t channelNum = N;                                  // Number of multiplexer channels.
  static constexpr uint32_t sensorWakeupTime = Time::secToMs(10U);          // Sensor wake-up time in milliseconds.
  static constexpr uint32_t filteringTime = Time::secToMs(2U);              // Filtering duration for analog values in milliseconds.
  static constexpr uint8_t readStartColors[3] = { 5U, 3U, 0U };               // RGB LED color values when a read operation starts.
  static constexpr uint32_t readTimeOffset = sensorWakeupTime + channelNum * filteringTime; // Time offset to account for wake-up and filtering times.

  const Multiplexer& multiplexer;                                           // Reference to the analog multiplexer object.
  RgbLedWrapper& rgbLed;                                                    // Reference to the RGB LED wrapper object.
  const uint8_t (&channels)[channelNum];                                    // Array of channels to read from.
  const uint32_t readTime;                                                  // Interval between consecutive reads in milliseconds.
  uint32_t eventTimer = 0U;                                                 // Class wide variable for universal timings.
  void (*dataSender)(const uint8_t (&data)[8]);                             // Function pointer for sending data.
  ReadState readState;                                                      // Current state of the moisture reader.
  uint8_t readIndex = 0U;                                                   // Current index of the channel being read.
  uint16_t moistureValue = 0U;                                              // Filtered analog moisture value.
};

template<uint8_t N>
MoistureReader<N>::MoistureReader(const Multiplexer& multiplexer, RgbLedWrapper& rgbLed, const uint8_t (&channels)[N], uint32_t readTime, void (*dataSender)(const uint8_t (&data)[8])) :
  multiplexer(multiplexer),
  rgbLed(rgbLed),
  channels(channels),
  readTime(readTime),
  dataSender(dataSender),
  readState(ReadState::IDLE) {}

template<uint8_t N>
bool MoistureReader<N>::init() {
  eventTimer = millis();
  return true;
}

template<uint8_t N>
bool MoistureReader<N>::run() {
  const uint32_t actualTime = millis();
  switch(readState) {
    case ReadState::IDLE: {
      if(Time::hasElapsed(actualTime, eventTimer, readTime)) {
        eventTimer = actualTime;
        multiplexer.enableRead();
        rgbLed.setColor(readStartColors[0], readStartColors[1], readStartColors[2], false);
        readState = ReadState::WAKEUP;
      }
    } break;
    case ReadState::WAKEUP: {
      if(Time::hasElapsed(actualTime, eventTimer, sensorWakeupTime)) {
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
      moistureValue = Analog::complementaryFilter10(multiplexer.analogReadAdvanced(), moistureValue);
      if(Time::hasElapsed(actualTime, eventTimer, filteringTime)) {
        if(dataSender != nullptr) {
          const uint8_t moistureH = static_cast<uint8_t>(moistureValue & 0xFF);
          const uint8_t moistureL = static_cast<uint8_t>((moistureValue >> 8U) & 0xFF);
          dataSender({ channels[readIndex], moistureH, moistureL, 0U, 0U, 0U, 0U, 0U });
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
  }
  return true;
}

template<uint8_t N>
void MoistureReader<N>::triggerImmediateMeasurement() {
  if(readState == ReadState::IDLE) {
    eventTimer = millis() - readTime;
  }
}
#endif // MOISTURE_READER_HPP
