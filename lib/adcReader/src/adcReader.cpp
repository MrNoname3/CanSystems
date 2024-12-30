#include "adcReader.hpp"

volatile bool AdcReader::adcReady = false;
const char AdcReader::MQTT_MSG_FRAME[] PROGMEM = {
  "{"
    "\"Analog\":[%hd,%hd,%hd,%hd],"
    "\"Voltage\":[%.2f,%.2f,%.2f,%.2f]"
  "}"
};

AdcReader::AdcReader(Connectivity& connectivity, const char* classID, uint16_t measureTime, uint8_t rdyPin, uint8_t sdaPin, uint8_t sclPin, uint8_t address) :
  MqttComBase(connectivity, classID),
  ADS(address),
  measureTime(measureTime),
  rdyPin(rdyPin),
  channel(0),
  adcValues{0},
  measureState(MeasureStates::IDLE),
  measureTimer(0),
  enableSending(false),
  mqttSendTime(0),
  mqttSendTimer(0),
  adsReadWdTime(measureTime * analogChannels),
  adsReadWdTimer(0),
  valuesReady(false)
{
  pinMode(rdyPin, INPUT_PULLUP);
  Wire.begin(sdaPin, sclPin);
}

bool AdcReader::begin() {
  const bool initAdc =  ADS.begin();
  if(initAdc) {
    // Set ALERT/RDY pin.
    ADS.setComparatorThresholdHigh(0x8000);
    ADS.setComparatorThresholdLow(0x0000);
    ADS.setComparatorQueConvert(0);
    ADS.setComparatorPolarity(1);
    attachInterrupt(digitalPinToInterrupt(rdyPin), intHandler, FALLING);
    measureState = MeasureStates::REQUEST_ADC;
  }
  adcReady = false;
  valuesReady = false;
  mqttSendTimer = millis();
  const bool adsHasError = ADS.getError() != ADS1X15_OK;
  return initAdc && !adsHasError;
}

void AdcReader::end() {
  valuesReady = false;
  detachInterrupt(digitalPinToInterrupt(rdyPin));
  measureState = MeasureStates::IDLE;
}

bool AdcReader::loop() {
  switch(measureState) {
    case MeasureStates::IDLE: {
      if(adcReady) {
        adcReady = false;
        measureState = MeasureStates::STORE_DATA;
      }
    } break;
    case MeasureStates::REQUEST_ADC: {
      ADS.requestADC(channel);
      adsReadWdTimer = millis();
      measureState = MeasureStates::IDLE;
    } break;
    case MeasureStates::STORE_DATA: {
      if(ADS.isReady()) {
        adcValues[channel] = ADS.getValue();
        if(channel == maxChannelNumber) {
          valuesReady = true;
        }
        measureTimer = millis();
        measureState = MeasureStates::MEASURE_DELAY;
      }
    } break;
    case MeasureStates::MEASURE_DELAY: {
      if((millis() - measureTimer) >= measureTime) {
        if(valuesReady && enableSending && (millis() - mqttSendTimer >= mqttSendTime)) {
          mqttSendTimer = millis();
          char dataOut[dataOutBufSize] = { '\0' };
          const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), MQTT_MSG_FRAME,
            adcValues[0], adcValues[1], adcValues[2], adcValues[3],
            ADS.toVoltage(adcValues[0]), ADS.toVoltage(adcValues[1]), ADS.toVoltage(adcValues[2]), ADS.toVoltage(adcValues[3]));
          const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
          if(!dataOutValid) { return false; }
          MqttComBase::messageSend(dataOut);
        }
        channel = (channel + 1) & maxChannelNumber;
        measureState = MeasureStates::REQUEST_ADC;
      }
    } break;
  }
  if(millis() - adsReadWdTimer >= adsReadWdTime) {
    adsReadWdTimer = millis();
    measureState = MeasureStates::REQUEST_ADC;
    return false;
  }
  return ADS.getError() == ADS1X15_OK ? true : false;
}

void AdcReader::messageReceived(uint8_t* payload, uint32_t length) {
  (void)payload;
  (void)length;
}

int16_t AdcReader::analogRead(Channel channel) {
  return adcValues[static_cast<uint8_t>(channel) & maxChannelNumber];
}

 float AdcReader::voltageRead(Channel channel) {
  return ADS.toVoltage(adcValues[static_cast<uint8_t>(channel) & maxChannelNumber]);
 }

void AdcReader::enableMqttSending(uint32_t interval) {
  const uint32_t minAllowedInterval = measureTime * analogChannels;
  mqttSendTime = interval > minAllowedInterval ? interval : minAllowedInterval;
  enableSending = true;
}

void AdcReader::disableMqttSending() {
  enableSending = false;
}

bool AdcReader::readyToRead() {
  return valuesReady;
}

void AdcReader::intHandler() {
  adcReady = true;
}