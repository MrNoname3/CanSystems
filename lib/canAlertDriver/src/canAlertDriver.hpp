#ifndef CAN_ALERT_DRIVER_HPP
#define CAN_ALERT_DRIVER_HPP

#include "canHandler.hpp"

class CanAlertDriver final : protected CanHandler::CanComBase {
public:
  CanAlertDriver(CanHandler& canHandler, uint32_t canId, Connectivity& connectivity, const char* classID, float tempOffset = 0.0F);
  ~CanAlertDriver() = default;
protected:
  virtual bool init() override;
  virtual bool run() override;
  virtual void canFrameReceived(CanHandler::CanFrame& canFrame) override;
private:
  static constexpr uint8_t dataOutBufSize = 96U;
  static const char PROGMEM HUM_TEMP_LDR_FRAME[];
  const float tempOffset;
};

#endif // CAN_ALERT_DRIVER_HPP