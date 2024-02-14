#ifdef PROJECT_CAN
#include "canToMqtt.hpp"

const char CanToMqtt::DEVICE_STATE_FRAME[] PROGMEM = {
  "{"
    "\"Time\":\"%s\","
    "\"Alive\":\"%s\""
  "}"
};

CanToMqtt::CanToMqtt(CanHandler& canHandler, uint32_t canId, Connectivity& connectivity, const char* classID) :
  CanComBase(canHandler, canId),
  MqttComBase(connectivity, classID),
  nodeAlive_(true)
{}

bool CanToMqtt::init() {
  beginCan();
  return true;
}

bool CanToMqtt::run(bool nodeAlive) {
  if(nodeAlive != nodeAlive_) {
    nodeAlive_ = nodeAlive;
    MqttComBase::sendResponse(nodeAlive_ ? MqttComBase::Response::ACK : MqttComBase::Response::NACK, static_cast<uint16_t>(CanCmd::PING));
  }
  loopCan();
  return true;
}

void CanToMqtt::canFrameReceived(CanHandler::CanFrame& canFrame) {
  const uint16_t command = static_cast<uint16_t>(canFrame.cmd);
  switch(command) {
    case static_cast<uint16_t>(CanCmd::PING): {} break;
    case static_cast<uint16_t>(CanCmd::RESTART): {} break;
    case static_cast<uint16_t>(CanCmd::BUTTON_EVENT): {} break;
    case static_cast<uint16_t>(CanCmd::OTA_START): {} break;
    case static_cast<uint16_t>(CanCmd::OTA_SEND): {} break;
    case static_cast<uint16_t>(CanCmd::OTA_END): {} break;
    case static_cast<uint16_t>(CanCmd::RGB_LED): {} break;
    default: { canMsgReceived(canFrame); } break;
  }
}

bool CanToMqtt::begin() {
  beginMqtt();
  return true;
}

bool CanToMqtt::loop() {
  loopMqtt();
  return true;
}

void CanToMqtt::messageReceived(uint8_t* payload, uint32_t length) {

  mqttMsgReceived(payload, length);
}

#endif // PROJECT_CAN