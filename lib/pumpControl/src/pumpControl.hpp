#ifndef PUMP_CONTROL_HPP
#define PUMP_CONTROL_HPP

#include <stdint.h>
#include "taskRunner.hpp"
#include "pcf8574.hpp"
#include "CircularBuffer.hpp"                                       /// Circular buffer class.

class PumpControl final : public TaskRunner {
public:
  PumpControl(PCF8574& pcf8574, uint8_t pwmPin, uint8_t intPin);
  ~PumpControl() = default;

  virtual void init() override;
  virtual void run() override;
  void createIrrigation(uint8_t irrigationInfo, uint8_t pwmValue, uint8_t repeatNum);
  void createIrrigation(uint8_t channel, uint8_t duration, uint8_t pwmValue, uint8_t repeatNum, bool checkFlow);

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
        uint8_t padding : 2;
      };
    };
    uint8_t pwmValue;
    uint8_t repeatNum;
    IrrigationQueueElement() : irrigationInfo(0U), pwmValue(0U), repeatNum(0U) {}
    IrrigationQueueElement(uint8_t irrigatinInfo, uint8_t pwmValue, uint8_t repeatNum) :
      irrigationInfo(irrigatinInfo), pwmValue(pwmValue), repeatNum(repeatNum) {}
    IrrigationQueueElement(uint8_t channel, uint8_t duration, bool checkFlow, uint8_t pwmValue, uint8_t repeatNum) :
      channel(channel &= 3U), duration(duration & 7U), checkFlow(static_cast<uint8_t>(checkFlow)), pwmValue(pwmValue), repeatNum(repeatNum) {}
  };

  static void irqHandler();
  bool selectChannel(uint8_t channel) const;

  PCF8574& pcf;
  const uint8_t pwmPin;
  const uint8_t intPin;
  static volatile uint16_t flowCounter;
  CircularBuffer<IrrigationQueueElement, 4> irrigationQueue;
};
#endif //PUMP_CONTROL_HPP