#include "rfHandler.hpp"
#include <ArduinoJson.h>                      /// Handle JSON files.

const char RfHandler::RF_MSG_FRAME[] PROGMEM = {
  "{"
    "\"Time\":\"%s\","
    "\"RfReceived\":"
    "{"
      "\"Data\":%llu,"
      "\"Bits\":%u,"
      "\"Protocol\":%u,"
      "\"Pulse\":%u"
    "}"
  "}"
};

RfHandler::RfHandler(const char* classID, uint8_t rxPin, uint8_t txPin) :
  MqttComBase(classID),
  rxPin_(rxPin),
  txPin_(txPin)
{
  if(rxPin_ != -1) { rfTransciever.enableReceive(digitalPinToInterrupt(rxPin_)); }
  if(txPin_ != -1) { rfTransciever.enableTransmit(txPin_); }
}

bool RfHandler::begin() { return true; }

bool RfHandler::loop() {
  if(rfTransciever.available()) {                                         // Check if RF data received.
    RFData rfData;
    rfData.data = rfTransciever.getReceivedValue();                       // Get received data.
    rfData.bitLength = rfTransciever.getReceivedBitlength();              // Get received data bit length.
    rfData.protocol = rfTransciever.getReceivedProtocol();                // Get receved data protocol.
    rfData.pulseLength = rfTransciever.getReceivedDelay();                // Get received data pulse length.
    rfTransciever.resetAvailable();                                       // Clear receive flag.

    char dataOut[dataOutSize] = { '\0' };
    const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), RF_MSG_FRAME, MqttComBase::getIsoTime(), rfData.data, rfData.bitLength, rfData.protocol, rfData.pulseLength);
    const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
    if(!dataOutValid) { return false; }
    MqttComBase::messageSend(dataOut);
  }
  return true;
}

void RfHandler::messageReceived(uint8_t* payload, uint32_t length) {
  StaticJsonDocument<dataInSize> rfJson;
  DeserializationError deserializationError = deserializeJson(rfJson, payload, length);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(!deSerResult) { return; }
  else {
    const uint64_t rfOutData = rfJson[F("Data")].as<uint64_t>();
    const uint32_t rfOutBitLength = rfJson[F("Bits")].as<uint32_t>();
    const uint32_t rfOutProtocol = rfJson[F("Protocol")].as<uint32_t>();
    const uint32_t rfOutPulseLength = rfJson[F("Pulse")].as<uint32_t>();
    if(rfOutProtocol != 0) { rfTransciever.setProtocol(rfOutProtocol); }
    if(rfOutPulseLength != 0) { rfTransciever.setPulseLength(rfOutPulseLength); }
    if(rfOutData != 0 && rfOutBitLength != 0) { rfTransciever.send(rfOutData, rfOutBitLength); }
  }
}