#ifndef PUMP_CONTROL_HPP
#define PUMP_CONTROL_HPP

#include <stdint.h>
#include "taskRunner.hpp"
#include "pcf8574.hpp"
#include "CircularBuffer.hpp"                                       /// Circular buffer class.

class PumpControl final : public TaskRunner {
public:
  PumpControl(PCF8574& pcf8574, uint8_t pwmPin, uint8_t intPin, uint8_t currentSensePin, void (*reportError)(uint8_t errCode));
  ~PumpControl() = default;

  virtual void init() override;
  virtual void run() override;
  void createIrrigation(uint8_t irrigationInfo, uint8_t pwmValue, uint8_t repeatNum);
  void createIrrigation(uint8_t channel, uint8_t duration, bool checkFlow, bool checkCurrent, uint8_t pwmValue, uint8_t repeatNum);
  int16_t calculateCurrent() const;

  PumpControl(const PumpControl&) = delete;               // Define copy constructor.
  PumpControl& operator=(const PumpControl&) = delete;    // Define copy assignment operator.
  PumpControl(PumpControl&&) = delete;                    // Define move constructor.
  PumpControl& operator=(PumpControl&&) = delete;         // Define move assignment operator.
private:
  struct __attribute__((packed))
  IrrigationQueueElement {
    union {
      uint8_t irrigationInfo;
      struct {
        uint8_t channel : 2;
        uint8_t duration : 3;
        uint8_t checkFlow : 1;
        uint8_t checkCurrent : 1;
        uint8_t padding : 1;
      };
    };
    uint8_t pwmValue;
    uint8_t repeatNum;
    IrrigationQueueElement() : irrigationInfo(0U), pwmValue(0U), repeatNum(0U) {}
    IrrigationQueueElement(uint8_t irrigatinInfo, uint8_t pwmValue, uint8_t repeatNum) :
      irrigationInfo(irrigatinInfo),
      pwmValue(pwmValue),
      repeatNum(repeatNum)
    {
    }
    IrrigationQueueElement(uint8_t channel, uint8_t duration, bool checkFlow, bool checkCurrent, uint8_t pwmValue, uint8_t repeatNum) :
      channel(channel &= 3U),
      duration(duration & 7U),
      checkFlow(static_cast<uint8_t>(checkFlow)),
      checkCurrent(static_cast<uint8_t>(checkCurrent)),
      pwmValue(pwmValue),
      repeatNum(repeatNum)
    {
    }
  };

  enum class IrrigationState : uint8_t {
    IDLE = 0,
    RUN,
    STOP,
    ERROR
  };

  enum class ERROR : uint8_t {
    NONE          = 0U,           // No error.
    CH_SELECT     = 1 << 0U,      // Channel select error.
    FLOW_STUCK    = 1 << 1U,      // Flow meter stuck error.
    FLOW_OVERRUN  = 1 << 2U,      // Flow meter counts, when it should not.
    PUMP_OVERRUN  = 1 << 3U,      // Pump takes current, when it should not.
    PUMP_OC       = 1 << 4U,      // Pump overcurrent error.
    PUMP_UC       = 1 << 5U,      // Pump undercurrent error.
    QUEUE_FULL    = 1 << 6U       // Irrigation queue is full.
  };

  static void irqHandler();
  bool selectChannel(uint8_t channel) const;
  void filterAnalogValue();
  void setError(ERROR err);
  const uint8_t getError();
  void createIrrigation(IrrigationQueueElement irrigationElement);

  PCF8574& pcf;
  const uint8_t pwmPin;
  const uint8_t intPin;
  const uint8_t currentSensePin;
  static volatile uint16_t flowCounter;
  uint16_t prevFlowCounter;
  CircularBuffer<IrrigationQueueElement, 4> irrigationQueue;
  IrrigationState irrigationState;
  uint16_t analogValue;
  uint32_t irrigationTimer;
  static constexpr uint16_t errorCheckTime = 1000U;
  uint32_t errorCheckTimer;
  uint8_t error;
  void (*reportError)(uint8_t errCode);
  static constexpr uint8_t maxAllowedStandbyCurrent = 100U; // mA
};
#endif //PUMP_CONTROL_HPP