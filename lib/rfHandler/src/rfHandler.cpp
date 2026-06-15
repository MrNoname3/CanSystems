#include "rfHandler.hpp"
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "common.hpp"                                               /// Common definitions and functions.

bool RfHandler::publishDiscovery() { // NOLINT(readability-convert-member-functions-to-static)
  using HA = Connectivity::HADiscovery;
  const HA::EntityConfig config = HA::EntityConfig::sensor(
      PSTR("RF Received"), PSTR("{{ value_json.RfReceived.Data }}"),
      nullptr, HA::StateClass::none, HA::DeviceClass::none,
      PSTR("mdi:remote"), PSTR("{{ value_json.RfReceived | tojson }}"));
  return doPublishEntityDiscovery(config);
}

RfHandler::RfHandler(Connectivity& connectivity, const char* subtopic, uint8_t rfRxPin, uint8_t rfTxPin) :
  MqttBase(connectivity, subtopic),
  rfRxPin(rfRxPin),
  rfTxPin(rfTxPin),
  dataCheckTimer(0U) {
  pinMode(this->rfRxPin, INPUT_PULLUP);
  rfTransceiver.enableReceive(digitalPinToInterrupt(this->rfRxPin));
  rfTransceiver.enableTransmit(this->rfTxPin);
}

bool RfHandler::run() {
  const uint32_t actualTime = millis();
  if(rfTransceiver.available()) {
    RfData actualRfData(rfTransceiver.getReceivedValue(), rfTransceiver.getReceivedBitlength(),
                        rfTransceiver.getReceivedProtocol(), rfTransceiver.getReceivedDelay());
    rfTransceiver.resetAvailable();

    // If timer is expired, clear old data to pass the next filter.
    if(Time::hasElapsed(actualTime, dataCheckTimer, dataCheckTime)) {
      lastRfData = RfData();
    }

    // Filter repeated data.
    if((lastRfData.data != actualRfData.data) || (lastRfData.bitLength != actualRfData.bitLength) || (lastRfData.protocol != actualRfData.protocol)) {
      char dataOut[dataOutBufSize] = { '\0' };
      // %llu expects unsigned long long; uint64_t is that on ESP but only unsigned long on the
      // LP64 host, so the cast keeps the format and the argument in agreement on every platform.
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), rfMessageFrame, static_cast<unsigned long long>(actualRfData.data), actualRfData.bitLength, actualRfData.protocol, actualRfData.pulseLength);
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
  if(dataJsonVar.is<uint64_t>() && bitsJsonVar.is<uint32_t>() && protocolJsonVar.is<uint32_t>() && pulseJsonVar.is<uint32_t>()) {
    const uint64_t rfOutData = dataJsonVar.as<uint64_t>();
    const uint32_t rfOutBitLength = bitsJsonVar.as<uint32_t>();
    const uint32_t rfOutProtocol = protocolJsonVar.as<uint32_t>();
    const uint32_t rfOutPulseLength = pulseJsonVar.as<uint32_t>();
    if(rfOutProtocol > 0U) {
      rfTransceiver.setProtocol(static_cast<int>(rfOutProtocol));
    }
    if(rfOutPulseLength > 0U) {
      rfTransceiver.setPulseLength(static_cast<int>(rfOutPulseLength));
    }
    if(rfOutData > 0U && rfOutBitLength > 0U) {
      rfTransceiver.send(rfOutData, rfOutBitLength);
    }
  }
}
