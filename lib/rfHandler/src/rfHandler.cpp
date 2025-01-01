#include "rfHandler.hpp"
#include <ArduinoJson.h>                      /// Handle JSON files.

const char RfHandler::RF_MSG_FRAME[] PROGMEM = {
  "{"
    "\"RfReceived\":"
    "{"
      "\"Data\":%llu,"
      "\"Bits\":%u,"
      "\"Protocol\":%u,"
      "\"Pulse\":%u"
    "}"
  "}"
};

RfHandler::RfHandler(Connectivity& connectivity, const char* classID, uint8_t rxPin, uint8_t txPin) :
  MqttBase(connectivity, classID),
  rxPin_(rxPin),
  txPin_(txPin)
{
  pinMode(rxPin_, INPUT_PULLUP);
  rfTransciever.enableReceive(digitalPinToInterrupt(rxPin_));
  rfTransciever.enableTransmit(txPin_);
}

bool RfHandler::init() { return true; }

bool RfHandler::run() {
  if(rfTransciever.available()) {                                         // Check if RF data received.
    static RFData rfDataOld;                                              // Save old data.
    static uint32_t dataCheckTimer = 0;                                   // Serial data send cooldown timer.
    RFData rfData;
    rfData.data = rfTransciever.getReceivedValue();                       // Get received data.
    rfData.bitLength = rfTransciever.getReceivedBitlength();              // Get received data bit length.
    rfData.protocol = rfTransciever.getReceivedProtocol();                // Get receved data protocol.
    rfData.pulseLength = rfTransciever.getReceivedDelay();                // Get received data pulse length.
    rfTransciever.resetAvailable();                                       // Clear receive flag.

    if(millis() - dataCheckTimer >= 100) {                                // If timer is expired, clear old data to pass the next filter.
      rfDataOld.data = 0;
      rfDataOld.bitLength = 0;
      rfDataOld.protocol = 0;
    }

    // Filter repeated data.
    if((rfDataOld.data != rfData.data) || (rfDataOld.bitLength != rfData.bitLength) || (rfDataOld.protocol != rfData.protocol)) {
      char dataOut[dataOutBufSize] = { '\0' };
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), RF_MSG_FRAME, rfData.data, rfData.bitLength, rfData.protocol, rfData.pulseLength);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return false; }
      if(!MqttBase::sendMessage(dataOut)) { return false; }
      rfDataOld = rfData;                                                 // Save sent data to use it for filtering.
    }
    dataCheckTimer = millis();                                            // Reload timer.
  }
  return true;
}

void RfHandler::messageArrivedCallback(const uint8_t* payload, uint32_t length) {
  JsonDocument rfJson;
  DeserializationError deserializationError = deserializeJson(rfJson, payload, length);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(!deSerResult) { return; }
  JsonVariant dataJsonVar = rfJson[F("Data")];
  JsonVariant bitsJsonVar = rfJson[F("Bits")];
  JsonVariant protocolJsonVar = rfJson[F("Protocol")];
  JsonVariant pulseJsonVar = rfJson[F("Pulse")];
  if(dataJsonVar.is<uint64_t>() && bitsJsonVar.is<uint32_t>() &&
    protocolJsonVar.is<uint32_t>() && pulseJsonVar.is<uint32_t>()) {
    const uint64_t rfOutData = dataJsonVar.as<uint64_t>();
    const uint32_t rfOutBitLength = bitsJsonVar.as<uint32_t>();
    const uint32_t rfOutProtocol = protocolJsonVar.as<uint32_t>();
    const uint32_t rfOutPulseLength = pulseJsonVar.as<uint32_t>();
    if(rfOutProtocol != 0) { rfTransciever.setProtocol(rfOutProtocol); }
    if(rfOutPulseLength != 0) { rfTransciever.setPulseLength(rfOutPulseLength); }
    if(rfOutData != 0 && rfOutBitLength != 0) { rfTransciever.send(rfOutData, rfOutBitLength); }
  }
}