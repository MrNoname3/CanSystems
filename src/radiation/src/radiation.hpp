#ifdef PROJECT_RAD_RF
#ifndef RADIATION_HPP
#define RADIATION_HPP

#include "../../connectivity/src/connectivity.hpp"
#include <Arduino.h>                          /// Arduino libraries header.
#include <Ticker.h>                           /// Timer interrupt hadnler.

class Radiation final : public Connectivity::MqttComBase {
public:
  Radiation(Connectivity& connectivity, const char* classID, uint8_t sensorPin);

  /// @brief Destructor of the object.
  virtual ~Radiation() = default;

  virtual bool begin() override;

  void end();

  virtual bool loop() override;

  virtual void messageReceived(uint8_t* payload, uint32_t length) override;

  Radiation(const Radiation&) = delete;                       // Define copy constructor.
  Radiation& operator=(const Radiation&) = delete;            // Define copy assignment operator.
  Radiation(Radiation&&) = delete;                            // Define move constructor.
  Radiation& operator=(Radiation&&) = delete;                 // Define move assignment operator.

private:
  inline static IRAM_ATTR void counter();
  inline static IRAM_ATTR void measure();

private:
  Ticker measureTimer;
  const uint8_t sensorPin;
  static volatile uint16_t cpm;
  static volatile bool measureDone;
  static volatile uint16_t cpmToSend;
  static constexpr uint8_t dataOutBufSize = 64;
  static const char PROGMEM CPM_MSG_FRAME[];
};
#endif // RADIATION_HPP
#endif // PROJECT_RAD_RF