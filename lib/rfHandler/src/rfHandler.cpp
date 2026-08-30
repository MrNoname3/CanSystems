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
  // One command per pass: transmitting is a blocking, timing-critical bit-bang, so a burst must
  // not hold the cooperative loop for several frames' air time in a row. Done after the receive
  // side, because the transceiver goes deaf for the whole transmission.
  if(!pendingTx.isEmpty()) { transmitCommand(pendingTx.pop()); }
  return true;
}

void RfHandler::transmitCommand(const RfData& command) {
  if(command.protocol > 0U) {
    rfTransceiver.setProtocol(static_cast<int>(command.protocol));
  }
  if(command.pulseLength > 0U) {
    rfTransceiver.setPulseLength(static_cast<int>(command.pulseLength));
  }
  if(command.data > 0U && command.bitLength > 0U) {
    rfTransceiver.send(command.data, command.bitLength);
  }
}

void RfHandler::messageArrivedCallback(JsonDocument& payloadJson) { // NOLINT(readability-convert-member-functions-to-static)
  JsonVariant dataJsonVar = payloadJson[F("Data")];
  JsonVariant bitsJsonVar = payloadJson[F("Bits")];
  JsonVariant protocolJsonVar = payloadJson[F("Protocol")];
  JsonVariant pulseJsonVar = payloadJson[F("Pulse")];
  if(dataJsonVar.is<uint64_t>() && bitsJsonVar.is<uint32_t>() && protocolJsonVar.is<uint32_t>() && pulseJsonVar.is<uint32_t>()) {
    const uint32_t rfOutPulseLength = pulseJsonVar.as<uint32_t>();
    if((rfOutPulseLength != 0U) && ((rfOutPulseLength < minPulseLength) || (rfOutPulseLength > maxPulseLength))) {
      Logger::get()->printf_P(PSTR("[RF] Pulse length %u out of range, command ignored\r\n"), rfOutPulseLength);
      return;
    }
    // Queued rather than transmitted here: this callback runs inside PubSubClient::loop(), and
    // the transmission blocks for the frame's whole air time.
    if(pendingTx.isFull()) {
      Logger::get()->printf_P(PSTR("[RF] Transmit queue full, dropping the oldest command\r\n"));
    }
    pendingTx.put(RfData(dataJsonVar.as<uint64_t>(), bitsJsonVar.as<uint32_t>(),
                         protocolJsonVar.as<uint32_t>(), rfOutPulseLength));
  }
}
