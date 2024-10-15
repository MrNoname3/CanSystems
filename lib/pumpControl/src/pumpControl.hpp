#ifndef PUMP_CONTROL_HPP
#define PUMP_CONTROL_HPP

#include <stdint.h>
#include "taskRunner.hpp"
#include "pcf8574.hpp"

class PumpControl final : public TaskRunner {
public:
  PumpControl(const PCF8574& pcf8574, uint8_t pwmPin, uint8_t intPin);
  ~PumpControl() = default;

  virtual void init() override;
  virtual void run() override;

  PumpControl(const PumpControl&) = delete;               // Define copy constructor.
  PumpControl& operator=(const PumpControl&) = delete;    // Define copy assignment operator.
  PumpControl(PumpControl&&) = delete;                    // Define move constructor.
  PumpControl& operator=(PumpControl&&) = delete;         // Define move assignment operator.
private:
  static void irqHandler();

  const PCF8574& pcf;
  const uint8_t pwmPin;
  const uint8_t intPin;
  static volatile uint16_t flowCounter;
};
#endif //PUMP_CONTROL_HPP