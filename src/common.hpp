#ifndef COMMON_HPP
#define COMMON_HPP

#include "mqttComBase.hpp"
#include <Arduino.h>                          /// Arduino libraries header.
#include <ArduinoJson.h>                      /// Handle JSON files.

class Common : public MqttComBase {
public:
  Common(const char* classID) : MqttComBase(classID) {}

  /// @brief Destructor of the object.
  virtual ~Common() = default;

  Common(const Common&) = delete;                       // Define copy constructor.
  Common& operator=(const Common&) = delete;            // Define copy assignment operator.
  Common(Common&&) = delete;                            // Define move constructor.
  Common& operator=(Common&&) = delete;                 // Define move assignment operator.
private:

};

#endif // COMMON_HPP