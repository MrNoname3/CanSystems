#ifdef PROJECT_CAN
#ifndef CAN_ALERT_DRIVER_HPP
#define CAN_ALERT_DRIVER_HPP

#include "../../canHandler/src/canHandler.hpp"

class CanAlertDriver final : protected CanHandler::CanComBase {
public:
  enum class CanCmd : uint16_t {
    READ_HUM_TEMP_LDR
  };
  /// @brief Use base class constructor.
  using CanComBase::CanComBase;
  ~CanAlertDriver() = default;
protected:
  virtual bool init() override;
  virtual bool run() override;
  virtual void canFrameReceived(CanHandler::CanFrame& canFrame) override;

};

#endif // CAN_ALERT_DRIVER_HPP
#endif // PROJECT_CAN