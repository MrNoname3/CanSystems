#include "MqttComBase.hpp"
#include "connectivity.hpp"

bool MqttComBase::isOnline = false;

MqttComBase::MqttComBase(const char* classID) {
  strlcpy(this->classId, classID, sizeof(this->classId));
  Connectivity::registerCallback(this);
}

void MqttComBase::messageSend(const char* payload) const {
  if(mqttSender) {
    mqttSender(getClassId(), payload);
  }
}