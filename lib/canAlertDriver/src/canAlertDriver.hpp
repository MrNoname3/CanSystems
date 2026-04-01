#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "canMqttGateway.hpp"                                       /// Base class for CAN-MQTT communication.

/// @brief A driver class for handling alerts using CAN bus and MQTT communication.
class CanAlertDriver final : public CanMqttGateway {
private:
  static constexpr uint8_t dataOutBufSize = 56U;                    // Buffer size for outgoing data strings.

  // Format string for encoding temperature, humidity, and light data.
  static constexpr const char PROGMEM humTempLdrFrame[] = R"({"Temperature":%.2f,"Humidity":%hu,"Light":%hu})";

public:
  /// @brief Constructor to initialize the driver.
  /// @param canHandler Reference to the CAN handler managing the bus.
  /// @param canId The CAN ID associated with this device.
  /// @param connectivity Reference to the MQTT connectivity object.
  /// @param subTopic Pointer to the subtopic string to be associated with the instance.
  /// @param tempOffset Offset to adjust the temperature readings. Default is `0.0F`.
  CanAlertDriver(CanHandler& canHandler, uint32_t canId, Connectivity& connectivity, const char* subTopic, float tempOffset = 0.0F);

  /// @brief Default destructor.
  ~CanAlertDriver() = default;

  CanAlertDriver(const CanAlertDriver&) = delete;                       // Define copy constructor.
  CanAlertDriver& operator=(const CanAlertDriver&) = delete;            // Define copy assignment operator.
  CanAlertDriver(CanAlertDriver&&) = delete;                            // Define move constructor.
  CanAlertDriver& operator=(CanAlertDriver&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Perform any local initialization required by the driver.
  /// @return Always returns `true` for this implementation.
  bool initLocal() override { return true; }

  /// @brief Execute local periodic tasks for the driver.
  /// @return  Always returns `true` for this implementation.
  bool runLocal() override { return true; }

  /// @brief Process a received MQTT message.
  /// @param payloadJson The JSON document containing the received message payload.
  void processMessageArrived(JsonDocument& payloadJson) override;

  /// @brief Process a received CAN frame.
  /// @param canFrame The CAN frame containing command and data bytes.
  void processCanFrameArrived(const CanHandler::CanFrame& canFrame) override;

  const float tempOffset;                                           // Offset to adjust temperature readings.
};