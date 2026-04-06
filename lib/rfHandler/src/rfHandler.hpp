#ifndef RFHANDLER_HPP
#define RFHANDLER_HPP

#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include <Arduino.h>                                                /// Arduino libraries header.
#include "RCSwitch.h"                                               /// RF driver library.

/// @brief Class for handling RF communication and integrating with MQTT.
/// This class supports receiving and transmitting RF signals, filtering duplicate data, and sending the processed data via MQTT.
class RfHandler final : public MqttBase {
private:
  static constexpr uint8_t dataOutBufSize = 116U;                   // Size of the buffer used for outgoing MQTT data messages.
  static constexpr uint8_t dataCheckTime = 100U;                    // Minimum time interval (in milliseconds) for considering redundant RF data as new one.

  // Format string for the MQTT message containing RF data.
  static constexpr const char PROGMEM rfMessageFrame[] = R"({"RfReceived":{"Data":%llu,"Bits":%u,"Protocol":%u,"Pulse":%u}})";

public:
  /// @brief Constructs the RF handler object.
  /// @param connectivity Reference to the MQTT connectivity object.
  /// @param subtopic Subtopic for MQTT messages related to RF data.
  /// @param rfRxPin GPIO pin connected to the RF receiver.
  /// @param rfTxPin GPIO pin connected to the RF transmitter.
  RfHandler(Connectivity& connectivity, const char* subtopic, uint8_t rfRxPin, uint8_t rfTxPin);

  /// @brief Destructor of the object.
  ~RfHandler() override = default;

  /// @brief Initializes the RF handler. No additional setup is required in this implementation.
  /// @return Always returns `true`.
  bool init() override { return true; } // NOLINT(readability-convert-member-functions-to-static)

  /// @brief Executes the RF handling logic, including receiving and sending RF data.
  /// @return `true` if the operation was successful; otherwise, `false`.
  bool run() override;

  /// @brief Callback invoked when an MQTT message arrives, with the payload already parsed into a JSON document.
  /// Derived classes must implement this to handle incoming messages.
  /// @param payloadJson Reference to a `JsonDocument` containing the parsed payload of the incoming message.
  void messageArrivedCallback(JsonDocument& payloadJson) override;

  RfHandler(const RfHandler&) = delete;                       // Define copy constructor.
  RfHandler& operator=(const RfHandler&) = delete;            // Define copy assignment operator.
  RfHandler(RfHandler&&) = delete;                            // Define move constructor.
  RfHandler& operator=(RfHandler&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Struct for storing RF data and its associated metadata.
  struct __attribute__((packed))
  RfData {
    uint64_t data;                                  // RF signal data.
    uint32_t bitLength;                             // Number of bits in the RF signal.
    uint32_t protocol;                              // Protocol used for the RF signal.
    uint32_t pulseLength;                           // Pulse length for the RF signal.

    /// @brief Default constructor initializing the RF data to zero.
    RfData() :
      data(0U),
      bitLength(0U),
      protocol(0U),
      pulseLength(0U)
    {}

    /// @brief Constructor initializing RF data with specified values.
    /// @param data RF signal data.
    /// @param bitLength Number of bits in the RF signal.
    /// @param protocol Protocol used for the RF signal.
    /// @param pulseLength Pulse length for the RF signal.
    RfData(uint64_t data, uint32_t bitLength, uint32_t protocol, uint32_t pulseLength) :
      data(data),
      bitLength(bitLength),
      protocol(protocol),
      pulseLength(pulseLength)
    {}
  };

  RCSwitch rfTransceiver;                                     // RF driver object for sending and receiving RF signals.
  const uint8_t rfRxPin;                                      // GPIO pin connected to the RF receiver.
  const uint8_t rfTxPin;                                      // GPIO pin connected to the RF transmitter.
  RfData lastRfData;                                          // Last received RF data for duplicate filtering.
  uint32_t dataCheckTimer;                                    // Timer for filtering out repeated RF data.
};
#endif // RFHANDLER_HPP