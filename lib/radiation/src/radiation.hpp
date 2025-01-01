#ifndef RADIATION_HPP
#define RADIATION_HPP

#include "connectivity.hpp"
#include <Arduino.h>                          /// Arduino libraries header.
#include <Ticker.h>                           /// Timer interrupt hadnler.

class Radiation final : public MqttBase {
public:
  Radiation(Connectivity& connectivity, const char* classID, uint8_t sensorPin);

  /// @brief Destructor of the object.
  virtual ~Radiation() = default;

  virtual bool init() override;

  void end();

  virtual void run() override;

  virtual void messageArrivedCallback(const uint8_t* payload, uint32_t length) override;

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