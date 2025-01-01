#include "canAlertDriver.hpp"

const char CanAlertDriver::HUM_TEMP_LDR_FRAME[] PROGMEM = {
  "{"
    "\"Temperature\":%.2f,"
    "\"Humidity\":%hu,"
    "\"Light\":%hu"
  "}"
};

CanAlertDriver::CanAlertDriver(CanHandler& canHandler, uint32_t canId, Connectivity& connectivity, const char* classID, float tempOffset) :
  CanComBase::CanComBase(canHandler, canId, connectivity, classID),
  tempOffset(tempOffset)
{}

bool CanAlertDriver::init() { return true; }

bool CanAlertDriver::run() { return true; }

void CanAlertDriver::canFrameReceived(CanHandler::CanFrame& canFrame) {
  const uint16_t command = canFrame.cmd;
  switch(command) {
    case static_cast<uint16_t>(CanCmd::READ_HUM_TEMP_LDR): {
      const float temperature = static_cast<float>((static_cast<uint16_t>(canFrame.data[0]) << 0U) | (static_cast<uint16_t>(canFrame.data[1]) << 8U)) / 100.0F + tempOffset;
      const uint16_t humidity = (static_cast<uint16_t>(canFrame.data[2]) << 0U) | (static_cast<uint16_t>(canFrame.data[3]) << 8U);
      const uint16_t lightRaw = (static_cast<uint16_t>(canFrame.data[4]) << 0U) | (static_cast<uint16_t>(canFrame.data[5]) << 8U);
      const uint8_t light = static_cast<uint8_t>(lightRaw >> 2U); // TODO: send the full 2byte to the server.
      char dataOut[dataOutBufSize] = { '\0' };
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), HUM_TEMP_LDR_FRAME, temperature, humidity, light);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return; }
      if(!MqttBase::sendMessage(dataOut)) { return; /*Handler needed*/ }
    } break;
    default: {} break;
  }
}