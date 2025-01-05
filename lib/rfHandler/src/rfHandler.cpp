#include "rfHandler.hpp"
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "common.hpp"                                               /// Common definitions and functions.

const char RfHandler::rfMessageFrame[] PROGMEM = {
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

RfHandler::RfHandler(Connectivity& connectivity, const char* subtopic, uint8_t rfRxPin, uint8_t rfTxPin) :
  MqttBase(connectivity, subtopic),
  rfTransciever(),
  rfRxPin(rfRxPin),
  rfTxPin(rfTxPin),
  lastRfData(),
  dataCheckTimer(0U)
{
  pinMode(this->rfRxPin, INPUT_PULLUP);
  rfTransciever.enableReceive(digitalPinToInterrupt(this->rfRxPin));
  rfTransciever.enableTransmit(this->rfTxPin);
}

bool RfHandler::run() {
  const uint32_t actualTime = millis();
  if(rfTransciever.available()) {
    RfData actualRfData(rfTransciever.getReceivedValue(), rfTransciever.getReceivedBitlength(),
      rfTransciever.getReceivedProtocol(), rfTransciever.getReceivedDelay());
    rfTransciever.resetAvailable();

    // If timer is expired, clear old data to pass the next filter.
    if(Time::hasElapsed(actualTime, dataCheckTimer, dataCheckTime)) {
      lastRfData.data = 0U;
      lastRfData.bitLength = 0U;
      lastRfData.protocol = 0U;
    }

    // Filter repeated data.
    if((lastRfData.data != actualRfData.data) || (lastRfData.bitLength != actualRfData.bitLength) || (lastRfData.protocol != actualRfData.protocol)) {
      char dataOut[dataOutBufSize] = { '\0' };
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), rfMessageFrame, actualRfData.data, actualRfData.bitLength, actualRfData.protocol, actualRfData.pulseLength);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return false; }
      if(!MqttBase::sendMessage(dataOut)) { return false; }
      lastRfData = actualRfData;
    }
    dataCheckTimer = actualTime;
  }
  return true;
}

void RfHandler::messageArrivedCallback(JsonDocument& payloadJson) {
  JsonVariant dataJsonVar = payloadJson[F("Data")];
  JsonVariant bitsJsonVar = payloadJson[F("Bits")];
  JsonVariant protocolJsonVar = payloadJson[F("Protocol")];
  JsonVariant pulseJsonVar = payloadJson[F("Pulse")];
  if(dataJsonVar.is<uint64_t>() && bitsJsonVar.is<uint32_t>() &&
    protocolJsonVar.is<uint32_t>() && pulseJsonVar.is<uint32_t>()) {
    const uint64_t rfOutData = dataJsonVar.as<uint64_t>();
    const uint32_t rfOutBitLength = bitsJsonVar.as<uint32_t>();
    const uint32_t rfOutProtocol = protocolJsonVar.as<uint32_t>();
    const uint32_t rfOutPulseLength = pulseJsonVar.as<uint32_t>();
    if(rfOutProtocol > 0U) {
      rfTransciever.setProtocol(rfOutProtocol);
    }
    if(rfOutPulseLength > 0U) {
      rfTransciever.setPulseLength(rfOutPulseLength);
    }
    if(rfOutData > 0U && rfOutBitLength > 0U) {
      rfTransciever.send(rfOutData, rfOutBitLength);
    }
  }
}