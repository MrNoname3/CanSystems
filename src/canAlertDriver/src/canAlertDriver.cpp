#ifdef PROJECT_CAN
#include "canAlertDriver.hpp"

bool CanAlertDriver::init() { return true; }

bool CanAlertDriver::run() { return true; }

void CanAlertDriver::canFrameReceived(CanHandler::CanFrame& canFrame) {
  const uint16_t command = canFrame.cmd;
  switch(command) {
    case static_cast<uint16_t>(CanCmd::READ_HUM_TEMP_LDR): {

    } break;
    default: {} break;
  }
}

#endif // PROJECT_CAN