#ifndef MOISTURE_READER_HPP
#define MOISTURE_READER_HPP

#include "stdint.h"
#include "multiplexer.hpp"
#include "taskRunner.hpp"

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
  uint32_t readTimer;
  void (*dataSender)(const uint8_t (&data)[8]);
  static constexpr uint16_t sensorWakeupTime = static_cast<uint16_t>(10U * 1000U);  // 10 seconds.
  uint32_t sensorWakeupTimer;
  ReadState readState;
  uint8_t readIndex;
  static constexpr uint16_t filteringTime = static_cast<uint16_t>(2U * 1000U);      // 2 seconds.
  uint32_t filteringTimer;
  uint16_t moistureValue;
};
#include "moistureReader.tpp"
#endif // MOISTURE_READER_HPP