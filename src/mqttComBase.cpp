#include "MqttComBase.hpp"
#include "connectivity.hpp"

bool MqttComBase::isOnline = false;
std::function<void(const char*, const char*)> MqttComBase::mqttSender = nullptr;

MqttComBase::MqttComBase(const char* classID) {
  strlcpy(this->classId, classID, sizeof(this->classId));
  //Connectivity::registerCallback(this);
}

void MqttComBase::messageSend(const char* payload) const {
  if(mqttSender) {
    mqttSender(getClassId(), payload);
  }
}

void MqttComBase::setMqttSender(std::function<void(const char*, const char*)> senderFunction) {
  mqttSender = senderFunction;
}