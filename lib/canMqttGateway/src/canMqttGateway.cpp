#include "canMqttGateway.hpp"

const char CanMqttGateway::statusOnline[] PROGMEM = "ONLINE";
const char CanMqttGateway::statusOffline[] PROGMEM = "OFFLINE";
const char CanMqttGateway::statusRestarted[] PROGMEM = "RESTARTED";
const char CanMqttGateway::statusPrintTemplate[] PROGMEM = "[CAN] %s is %s!\r\n";
const char CanMqttGateway::statusFrame[] PROGMEM = {
  "{"
    "\"Status\":\"%s\""
  "}"
};
const char CanMqttGateway::buttonFrame[] PROGMEM = {
  "{"
    "\"Button\":%hu"
  "}"
};
const char CanMqttGateway::buildInfoFrame[] PROGMEM = {
  "{"
    "\"Firmware\":%hu,"
    "\"GitHash\":\"%x\""
  "}"
};
const char CanMqttGateway::otaFrame[] PROGMEM = {
  "{"
    "\"OTA\":\"%s\""
  "}"
};

CanMqttGateway::CanMqttGateway(CanHandler& canHandler, uint16_t clientCanId, Connectivity& connectivity, const char* subTopic) :
  CanBase(canHandler, clientCanId),
  MqttBase(connectivity, subTopic),
  clientPingTimer(0U),
  clientOfflineTimer(0U),
  clientOnline(true)
{}

bool CanMqttGateway::init() {
  (void)sendCanFrame(CanCmd::PING);
  clientPingTimer = clientOfflineTimer = millis();
  return initLocal();
}

bool CanMqttGateway::run() {
  handlePing();
  return runLocal();
}

void CanMqttGateway::handlePing() {
  const uint32_t actualTime = millis();
  if(Time::hasElapsed(actualTime, clientPingTimer, clientPingTime)) {
    (void)sendCanFrame(CanCmd::PING);
    clientPingTimer = millis();
  }
  const bool clientOnlineActual = Time::hasElapsed(actualTime, clientOfflineTimer, clientOfflineTime);
  if(clientOnline != clientOnlineActual) {
    clientOnline = clientOnlineActual;
    Serial.printf_P(statusPrintTemplate, MqttBase::getSubtopic(), clientOnline ? statusOnline : statusOffline);
    char dataOut[statusBufSize] = {'\0'};
    const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), statusFrame, clientOnline ? statusOnline : statusOffline);
    const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
    if(dataOutValid) {
      (void)MqttBase::sendMessage(dataOut);
    }
  }
}

void CanMqttGateway::messageArrivedCallback(JsonDocument& payloadJson) {
  processMessageArrived(payloadJson);
}

void CanMqttGateway::canFrameArrivedCallback(const CanHandler::CanFrame& canFrame) {
  clientPingTimer = clientOfflineTimer = millis();
  processCanFrameArrived(canFrame);
}