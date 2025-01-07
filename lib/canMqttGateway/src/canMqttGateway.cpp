#include "canMqttGateway.hpp"

CanMqttGateway::CanMqttGateway(CanHandler& canHandler, uint16_t clientCanId, Connectivity& connectivity, const char* subTopic) :
    CanBase(canHandler, clientCanId),
    MqttBase(connectivity, subTopic)
  {}