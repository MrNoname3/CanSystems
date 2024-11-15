#ifndef MOISTURE_READER_HPP
#define MOISTURE_READER_HPP

#include "stdint.h"                                                 /// Standard fixed-width integer types.
#include "multiplexer.hpp"                                          /// Analog multiplexer class.
#include "taskRunner.hpp"                                           /// Task runner class.
#include "common.hpp"                                               /// Common definitions and functions.

template<uint8_t N>
class MoistureReader final : public TaskRunner {
public:
  MoistureReader(const Multiplexer& multiplexer, const uint8_t (&channels)[N], uint32_t readTime, void (*dataSender)(const uint8_t (&data)[8]));
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
  const uint8_t (&channels)[N];
  const uint32_t readTime;
  uint32_t eventTimer;
  void (*dataSender)(const uint8_t (&data)[8]);
  static constexpr uint32_t sensorWakeupTime = TimeConverter::secToMs(10U);         // 10 seconds.
  ReadState readState;
  uint8_t readIndex;
  static constexpr uint32_t filteringTime = TimeConverter::secToMs(2U);             // 2 seconds.
  uint16_t moistureValue;
};
#include "moistureReader.tpp"
#endif // MOISTURE_READER_HPP