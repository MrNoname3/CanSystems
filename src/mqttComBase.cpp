#include "MqttComBase.hpp"
#include "connectivity.hpp"

bool MqttComBase::isOnline = false;
std::function<void(const char*, const char*)> MqttComBase::mqttSender = nullptr;

MqttComBase::MqttComBase(const char* classID) {
  strlcpy(this->classId, classID, sizeof(this->classId));
  Connectivity::registerCallback(this);
}

void MqttComBase::messageSend(const char* payload) const {
  if(mqttSender) {
    mqttSender(getClassId(), payload);
  }
}

void MqttComBase::setMqttSender(std::function<void(const char*, const char*)> senderFunction) {
  mqttSender = senderFunction;
}

bool MqttComBase::sendResponse(Response resp, uint16_t cmd) {
  static constexpr const uint8_t respBufSize = 28;
  char respBuf[respBufSize] = { '\0' };
  const int32_t respBufRealSize = snprintf_P(respBuf, sizeof(respBuf), PSTR("{""\"type\":%hu,""\"cmd\":%hu""}"), static_cast<uint8_t>(resp), cmd);
  const bool respBufValid = (respBufRealSize >= 0 && respBufRealSize < static_cast<int32_t>(sizeof(respBuf)));
  if(!respBufValid) { return false; }
  messageSend(respBuf);
  return true;
}