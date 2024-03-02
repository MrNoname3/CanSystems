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
  Wire.setClock(400000);                                                      // Set I2C bus speed.
  Wire.setWireTimeout(20000, true);                                           // Set I2C timeout to 20ms.
}

void AmbientSensor::begin() {
  serialPort.print(F("SI7021: "));
  const bool si7021BeginResult = si7021.begin();
  si7021BeginResult ? serialPort.println(CanHandler::OK_STATE) : serialPort.println(CanHandler::ERR_STATE);
  if(!si7021BeginResult) { return; }
  si7021.setHeater(false);
}

void AmbientSensor::loop() {
  static uint8_t lightValue = 0;
  { // Complementer filter calculation.
    static constexpr uint8_t adcInputFilterAlpha = 10;                          // Complementer filter ALPHA value.
    uint8_t lightRaw = analogRead(lightPin) >> 2;                               // Analog read and map from 10bit to 8bit.
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
      static_cast<uint8_t>((temperature >> 0) & 0xFF),
      static_cast<uint8_t>((temperature >> 8) & 0xFF),
      static_cast<uint8_t>((humidity >> 0) & 0xFF),
      static_cast<uint8_t>((humidity >> 8) & 0xFF),
      lightValue,
      0,
      0,
      0
    };
    canHandler.send(CanCmd::READ_HUM_TEMP_LDR, data);
    serialPort.print(F("L:"));
    serialPort.print(lightValue);
    serialPort.print(F(", T:"));
    serialPort.print(temperature);
    serialPort.print(F(", H:"));
    serialPort.println(humidity);
  }
}