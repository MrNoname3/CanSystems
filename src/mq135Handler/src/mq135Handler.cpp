#include "mq135Handler.hpp"

const char Mq135Handler::MQTT_MSG_FRAME[] PROGMEM = {
  "{"
    "\"Time\":\"%s\","
    "\"Gas\":"
    "{"
      "\"CO\":%.2f,"
      "\"Alcohol\":%.2f,"
      "\"CO2\":%.2f,"
      "\"Toluene\":%.2f,"
      "\"NH4\":%.2f,"
      "\"Acetone\":%.2f"
    "}"
  "}"
};

 Mq135Handler::Mq135Handler(Connectivity& connectivity, const char* classID, AdcReader& adcReader, AdcReader::Channel channel, uint32_t measureTime) :
  MqttComBase(connectivity, classID),
  adcReader(adcReader),
  channel(channel),
  mq135("", 5.0f, 12, -1, ""),
  measureTime(measureTime),
  gasReadState(GasReadState::IDLE),
  readIndex(0),
  gasValues{0.0f}
  {
    mq135.setRegressionMethod(1);
    mq135.setRL(1.0f);
    //mq135.setR0(1.2f);
    //mq135.setR0(21.18f);
    mq135.setR0(43.47f);
  }

  bool Mq135Handler::begin() {
    bool ret = true;
    readIndex = 0;
    gasReadState = GasReadState::IDLE;
    measureTimer = millis();
    return ret;
  }

  bool Mq135Handler::loop() {
    bool ret = true;
    switch(gasReadState) {
      case GasReadState::IDLE: {
        if((millis()- measureTimer >= measureTime) && adcReader.readyToRead()) {
          measureTimer = millis();
          gasReadState = GasReadState::READ;
        }
      } break;
      case GasReadState::CALIBRATION: {
        if(adcReader.readyToRead()) {
          startCalibration();
          gasReadState = GasReadState::IDLE;
        }
      } break;
      case GasReadState::READ: {
        mq135.setADC(getAnalogValue());
        mq135.setA(gasEquationValues[readIndex][static_cast<uint8_t>(EQ::A)]);
        mq135.setB(gasEquationValues[readIndex][static_cast<uint8_t>(EQ::B)]);
        gasValues[readIndex] = mq135.readSensor() + gasReadOffset[readIndex];
        readIndex++;
        if(readIndex == numGases) {
          gasReadState = GasReadState::SEND;
          readIndex = 0;
        }
      } break;
      case GasReadState::SEND: {
        //Serial.printf_P(PSTR("\r\nCO: %.2f ppm\r\nAlcohol: %.2f ppm\r\nCO2: %.2f ppm\r\nToluene: %.2f ppm\r\nNH4: %.2f ppm\r\nAcetone: %.2f ppm\r\n"),
        //  gasValues[0], gasValues[1], gasValues[2], gasValues[3], gasValues[4], gasValues[5]);
        char dataOut[dataOutBufSize] = { '\0' };
        const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), MQTT_MSG_FRAME, MqttComBase::getIsoTime(),
          gasValues[0], gasValues[1], gasValues[2], gasValues[3], gasValues[4], gasValues[5]);
        const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
        if(!dataOutValid) { return false; }
        MqttComBase::messageSend(dataOut);
        gasReadState = GasReadState::IDLE;
      } break;
    }
    return ret;
  }

  void Mq135Handler::messageReceived(uint8_t* payload, uint32_t length) {}

  bool Mq135Handler::startCalibration() {
    const uint16_t adcValue = getAnalogValue();
    mq135.setADC(adcValue);
    const float calcR0 = mq135.calibrate(ratioMQ135CleanAir);
    mq135.setR0(calcR0);
    const bool calSuccess = (!isinf(calcR0)) && (calcR0 > 0);
    Serial.println(calcR0);
    return calSuccess;
  }

  uint16_t Mq135Handler::getAnalogValue() {
    // Maximum possible value of MQ-135 sensor.
    // The analog input channel has a voltage divider to limit the maximum signal voltage from 5V to 3.3V.
    constexpr int16_t maxAnalogValue = 17600;
    int16_t adcValue = adcReader.analogRead(channel);
    adcValue = adcValue < 0 ? 0 : adcValue;                                                       // Avoid negative values.
    adcValue = adcValue > maxAnalogValue ? maxAnalogValue : adcValue;                             // Set maximum allowed value.
    const uint16_t result = static_cast<uint16_t>(map(adcValue, 0U, maxAnalogValue, 0U, 4095U));  // Limit the result to 12bit.
    return result;
  }