#include "ambientSensor.hpp"
#include <Wire.h>

AmbientSensor::AmbientSensor(HardwareSerial& serial, CanHandler& canHandler, uint8_t lightPin, uint32_t measurePeriod) :
  si7021(),
  serialPort(serial),
  canHandler(canHandler),
  lightPin(lightPin),
  measurePeriod(measurePeriod)
{
  analogReference(DEFAULT);                                                   // Setup analog reference to 5V.
  bitSet(ADCSRA, ADPS2);                                                      // Fast ADC, set prescaler to 16.
  bitSet(ADCSRA, ADPS1);
  bitClear(ADCSRA, ADPS0);
  Wire.setClock(400000U);                                                     // Set I2C bus speed.
  Wire.setWireTimeout(20000U, true);                                          // Set I2C timeout to 20ms.
}

void AmbientSensor::init() {
  serialPort.print(F("SI7021: "));
  const bool si7021BeginResult = si7021.begin();
  si7021BeginResult ? serialPort.println(CanHandler::OK_STATE) : serialPort.println(CanHandler::ERR_STATE);
  if(!si7021BeginResult) { return; }
  si7021.setHeater(false);
}

void AmbientSensor::run() {
  static uint8_t lightValue = 0U;
  { // Complementer filter calculation.
    static constexpr uint8_t adcInputFilterAlpha = 10U;                         // Complementer filter ALPHA value.
    uint8_t lightRaw = analogRead(lightPin) >> 2U;                              // Analog read and map from 10bit to 8bit.
    lightValue = ((adcInputFilterAlpha * lightRaw) + (100U - adcInputFilterAlpha) * lightValue) / 100U;
  }
  if(!si7021.sensorExists()) { return; }
  static uint32_t measureTimer = millis();
  if(millis() - measureTimer >= measurePeriod) {
    measureTimer = millis();
    const int16_t temperature = si7021.getCelsiusHundredths();
    const uint16_t humidity = si7021.getHumidityPercent();
    const bool dataInvalid = Wire.getWireTimeoutFlag();
    if(dataInvalid) {
      Wire.clearWireTimeoutFlag();
      return;
    }
    const uint8_t data[8] = {
      static_cast<uint8_t>((temperature >> 0U) & 0xFF),
      static_cast<uint8_t>((temperature >> 8U) & 0xFF),
      static_cast<uint8_t>((humidity >> 0U) & 0xFF),
      static_cast<uint8_t>((humidity >> 8U) & 0xFF),
      lightValue,
      0U,
      0U,
      0U
    };
    canHandler.send(CanCmd::READ_HUM_TEMP_LDR, data);
    serialPort.print(F("T:"));
    serialPort.print(temperature);
    serialPort.print(F(", H:"));
    serialPort.print(humidity);
    serialPort.print(F(", L:"));
    serialPort.println(lightValue);
  }
}