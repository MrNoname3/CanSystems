#ifndef RFHANDLER_HPP
#define RFHANDLER_HPP

#include "../../connectivity.hpp"
#include <Arduino.h>                          /// Arduino libraries header.
#include <RCSwitch.h>                         /// RF driver library.

class RfHandler final : public Connectivity::MqttComBase {
public:
  RfHandler(Connectivity& connectivity, const char* classID, uint8_t rxPin, uint8_t txPin);

  /// @brief Destructor of the object.
  virtual ~RfHandler() = default;

  virtual bool begin() override;

  virtual bool loop() override;

  virtual void messageReceived(uint8_t* payload, uint32_t length) override;

  RfHandler(const RfHandler&) = delete;                       // Define copy constructor.
  RfHandler& operator=(const RfHandler&) = delete;            // Define copy assignment operator.
  RfHandler(RfHandler&&) = delete;                            // Define move constructor.
  RfHandler& operator=(RfHandler&&) = delete;                 // Define move assignment operator.

private:
  /// @brief RF data transciev struct.
  struct __attribute__((packed))
  RFData {
    uint64_t data;                              // RF data.
    uint32_t bitLength;                         // RF data bit length.
    uint32_t protocol;                          // RF protocol.
    uint32_t pulseLength;                       // RF pulse length.
    RFData() : data(0), bitLength(0), protocol(0), pulseLength(0) {}
  };

  RCSwitch rfTransciever;                                      // RF modules driver object.
  const uint8_t rxPin_;
  const uint8_t txPin_;
  static constexpr uint8_t dataOutBufSize = 140;
  static constexpr uint8_t dataInSize = 92;
  static const char PROGMEM RF_MSG_FRAME[];
};
#endif // RFHANDLER_HPP