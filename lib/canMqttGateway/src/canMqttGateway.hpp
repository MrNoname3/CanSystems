#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "canHandler.hpp"                                           /// CAN handler library.
#include <ArduinoJson.h>                                            /// Handle JSON files.

class CanMqttGateway : public CanBase, public MqttBase {
public:

  [[nodiscard]] virtual bool init() = 0;

  [[nodiscard]] virtual bool run() = 0;

  virtual void processMessageArrived(JsonDocument& payloadJson) = 0;

  virtual void processCanFrameArrived(const CanHandler::CanFrame& canFrame) = 0;

  CanMqttGateway(const CanMqttGateway&) = delete;                       // Define copy constructor.
  CanMqttGateway& operator=(const CanMqttGateway&) = delete;            // Define copy assignment operator.
  CanMqttGateway(CanMqttGateway&&) = delete;                            // Define move constructor.
  CanMqttGateway& operator=(CanMqttGateway&&) = delete;                 // Define move assignment operator

protected:
  CanMqttGateway(CanHandler& canHandler, uint16_t clientCanId,
    Connectivity& connectivity, const char* subTopic);

  /// @brief Virtual destructor of the object.
  virtual ~CanMqttGateway() = default;

private:
  virtual inline void messageArrivedCallback(JsonDocument& payloadJson) override {
    // Do something with the data.
    processMessageArrived(payloadJson);
  }

  virtual inline void canFrameArrivedCallback(const CanHandler::CanFrame& canFrame) override {
    // Do something with the data.
    processCanFrameArrived(canFrame);
  }
};