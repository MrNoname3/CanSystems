#ifndef RADIATION_HPP
#define RADIATION_HPP

#include "connectivity.hpp"
#include <Arduino.h>                          /// Arduino libraries header.
#include <ArduinoJson.h>                      /// Handle JSON files.
#include <Ticker.h>                           /// Timer interrupt hadnler.

class Radiation : public Connectivity::MqttComBase {
public:
  Radiation(const char* classID, uint8_t sensorPin) : MqttComBase(classID), measureTimer(), sensorPin(sensorPin) {
    pinMode(sensorPin, INPUT_PULLUP); // INPUT_PULLUP is only for test, for real sensor it should be only INPUT.
  }

  /// @brief Destructor of the object.
  virtual ~Radiation() = default;

  void begin() {
    constexpr const uint16_t measureTime = 60000;  //millisec
    attachInterrupt(digitalPinToInterrupt(sensorPin), counter, FALLING);
    measureTimer.attach_ms(measureTime, measure);
    cpm = 0;
  }

  void end() {
    detachInterrupt(digitalPinToInterrupt(sensorPin));
    measureTimer.detach();
    cpm = 0;
    measureDone = false;
  }

  bool loop() {
    if(measureDone) {
      measureDone = false;
      char dataOut[16] = { '\0' };
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), PSTR("{""\"cpm\":%hu""}"), cpmToSend);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return false; }
      MqttComBase::messageSend(dataOut);
    }
    return true;
  }

  virtual void messageReceived(uint8_t* payload, uint32_t length) override {}

  Radiation(const Radiation&) = delete;                       // Define copy constructor.
  Radiation& operator=(const Radiation&) = delete;            // Define copy assignment operator.
  Radiation(Radiation&&) = delete;                            // Define move constructor.
  Radiation& operator=(Radiation&&) = delete;                 // Define move assignment operator.

private:
  inline static IRAM_ATTR void counter() __attribute__((always_inline)) { cpm++; }
  inline static IRAM_ATTR void measure() __attribute__((always_inline)) {
    cli();
    cpmToSend = cpm;
    cpm = 0;
    measureDone = true;
    sei();
  }

private:
  Ticker measureTimer;
  const uint8_t sensorPin;
  static volatile uint16_t cpm;
  static volatile bool measureDone;
  static volatile uint16_t cpmToSend;
};

volatile uint16_t Radiation::cpm = 0;
volatile bool Radiation::measureDone = false;
volatile uint16_t Radiation::cpmToSend = 0;

#endif // RADIATION_HPP