#pragma once

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "canMqttGateway.hpp"

class CanAlertDriver final : public CanMqttGateway {
private:
  static constexpr uint8_t dataOutBufSize = 56U;

public:
  CanAlertDriver(CanHandler& canHandler, uint32_t canId, Connectivity& connectivity, const char* classID, float tempOffset = 0.0F);
  ~CanAlertDriver() = default;

  virtual bool init() override { return true; }
  virtual bool run() override { return true; }

  CanAlertDriver(const CanAlertDriver&) = delete;                       // Define copy constructor.
  CanAlertDriver& operator=(const CanAlertDriver&) = delete;            // Define copy assignment operator.
  CanAlertDriver(CanAlertDriver&&) = delete;                            // Define move constructor.
  CanAlertDriver& operator=(CanAlertDriver&&) = delete;                 // Define move assignment operator.

private:
  virtual void processMessageArrived(JsonDocument& payloadJson) override;

  virtual void processCanFrameArrived(const CanHandler::CanFrame& canFrame) override;

  static const char PROGMEM humTempLdrFrame[];

  const float tempOffset;
};