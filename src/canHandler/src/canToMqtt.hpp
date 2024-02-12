#ifdef PROJECT_CAN
#ifndef CAN_TO_MQTT_HPP
#define CAN_TO_MQTT_HPP

#include "canHandler.hpp"
#include "../../connectivity.hpp"

class CanToMqtt : protected CanHandler::CanComBase, protected Connectivity::MqttComBase {
public:
  CanToMqtt(const CanToMqtt&) = delete;                       // Define copy constructor.
  CanToMqtt& operator=(const CanToMqtt&) = delete;            // Define copy assignment operator.
  CanToMqtt(CanToMqtt&&) = delete;                            // Define move constructor.
  CanToMqtt& operator=(CanToMqtt&&) = delete;                 // Define move assignment operator.
protected:
  CanToMqtt(CanHandler& canHandler, uint32_t canId, Connectivity& connectivity, const char* classID);
  /// @brief Destructor of the object.
  virtual ~CanToMqtt() = default;
  virtual bool init() override;
  virtual bool run(bool nodeAlive) override;
  virtual void canFrameReceived(CanHandler::CanFrame& canFrame) override;
  virtual bool begin() override;
  virtual bool loop() override;
  virtual void messageReceived(uint8_t* payload, uint32_t length) override;
  virtual void beginCan() = 0;
  virtual void loopCan() = 0;
  virtual void canMsgReceived(CanHandler::CanFrame& canFrame) = 0;
  virtual void beginMqtt() = 0;
  virtual void loopMqtt() = 0;
  virtual void mqttMsgReceived(uint8_t* payload, uint32_t length) = 0;
private:
  bool nodeAlive_;
  static constexpr uint8_t dataOutBufSize = 128;
  static const char PROGMEM DEVICE_STATE_FRAME[];
};
#endif // CAN_TO_MQTT_HPP
#endif // PROJECT_CAN