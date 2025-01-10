#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "canHandler.hpp"                                           /// CAN handler library.
#include <ArduinoJson.h>                                            /// Handle JSON files.

class CanMqttGateway : public CanBase, public MqttBase {
private:
  static constexpr uint32_t clientPingTime = Time::secToMs(1U);
  static constexpr uint32_t clientOfflineTime = Time::secToMs(2U);
  static constexpr uint8_t statusBufSize = 64U;

public:

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
  virtual bool init() override;

  virtual bool run() override;

  void handlePing();

  [[nodiscard]] virtual bool initLocal() = 0;

  [[nodiscard]] virtual bool runLocal() = 0;

  virtual void messageArrivedCallback(JsonDocument& payloadJson) override;

  virtual void canFrameArrivedCallback(const CanHandler::CanFrame& canFrame) override;

  static const char PROGMEM statusOnline[];
  static const char PROGMEM statusOffline[];
  static const char PROGMEM statusRestarted[];
  static const char PROGMEM statusPrintTemplate[];
  static const char PROGMEM statusFrame[];

  static const char PROGMEM buttonFrame[];
  static const char PROGMEM buildInfoFrame[];
  static const char PROGMEM otaFrame[];

  uint32_t clientPingTimer;
  uint32_t clientOfflineTimer;
  bool clientOnline;
};