#ifndef MQTT_COM_BASE_HPP
#define MQTT_COM_BASE_HPP

#include <functional>

class Connectivity; // Forward declaration

class MqttComBase {
protected:
  MqttComBase(const char* classID);
  virtual ~MqttComBase() = default;
  void messageSend(const char* payload) const;
public:
  virtual void messageReceived(uint8_t* payload, uint32_t length) = 0;
  const char* getClassId() const { return classId; }
  static void setMqttSender(std::function<void(const char*, const char*)> senderFunction);
  static void setConState(bool state) { isOnline = state; }

  MqttComBase(const MqttComBase&) = delete;                       // Define copy constructor.
  MqttComBase& operator=(const MqttComBase&) = delete;            // Define copy assignment operator.
  MqttComBase(MqttComBase&&) = delete;                            // Define move constructor.
  MqttComBase& operator=(MqttComBase&&) = delete;                 // Define move assignment operator.
protected:
  static bool getConState() { return isOnline; }
private:
  char classId[16];
  static std::function<void(const char*, const char*)> mqttSender;
  static bool isOnline;
};


#endif // MQTT_COM_BASE_HPP