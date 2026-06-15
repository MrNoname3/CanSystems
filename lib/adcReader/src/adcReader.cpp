#include "adcReader.hpp"
#include "common.hpp"                                               /// Common definitions and functions.

volatile bool AdcReader::adcReady = false;

AdcReader::AdcReader(Connectivity& connectivity, const char* subTopic, uint16_t measureTime, uint8_t rdyPin, uint8_t sdaPin, uint8_t sclPin, uint8_t address) :
  MqttBase(connectivity, subTopic),
  ADS(address),
  measureTime(measureTime),
  rdyPin(rdyPin),
  channel(0U),
  adcValues{ 0 },
  measureState(MeasureStates::IDLE),
  measureTimer(0U),
  enableSending(false),
  mqttSendTime(0U),
  mqttSendTimer(0U),
  adsReadWdTime(measureTime * analogChannels),
  adsReadWdTimer(0U),
  valuesReady(false) {
  pinMode(rdyPin, INPUT_PULLUP);
  Wire.begin(sdaPin, sclPin);
}

bool AdcReader::init() {
  const bool initAdc = ADS.begin();
  if(initAdc) {
    // Set ALERT/RDY pin.
    ADS.setComparatorThresholdHigh(0x8000);
    ADS.setComparatorThresholdLow(0x0000);
    ADS.setComparatorQueConvert(0U);
    ADS.setComparatorPolarity(1U);
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

bool AdcReader::run() {
  const uint32_t actualTime = millis();
  switch(measureState) {
    case MeasureStates::IDLE: {
      if(adcReady) {
        adcReady = false;
        measureState = MeasureStates::STORE_DATA;
      }
    } break;
    case MeasureStates::REQUEST_ADC: {
      ADS.requestADC(channel);
      adsReadWdTimer = actualTime;
      measureState = MeasureStates::IDLE;
    } break;
    case MeasureStates::STORE_DATA: {
      if(ADS.isReady()) {
        adcValues[channel] = ADS.getValue();
        if(channel == maxChannelNumber) {
          valuesReady = true;
        }
        measureTimer = actualTime;
        measureState = MeasureStates::MEASURE_DELAY;
      }
    } break;
    case MeasureStates::MEASURE_DELAY: {
      if(Time::hasElapsed(actualTime, measureTimer, measureTime)) {
        if(valuesReady && enableSending && Time::hasElapsed(actualTime, mqttSendTimer, mqttSendTime)) {
          mqttSendTimer = actualTime;
          char dataOut[dataOutBufSize] = { '\0' };
          // clang-format off
          const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), mqttMsgFrame,
            adcValues[0], adcValues[1], adcValues[2], adcValues[3],
            ADS.toVoltage(adcValues[0]), ADS.toVoltage(adcValues[1]), ADS.toVoltage(adcValues[2]), ADS.toVoltage(adcValues[3]));
          // clang-format on
          const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
          if(!dataOutValid) { return false; }
          if(!MqttBase::sendMessage(dataOut)) { return false; }
        }
        channel = (channel + 1U) & maxChannelNumber;
        measureState = MeasureStates::REQUEST_ADC;
      }
    } break;
  }
  if(Time::hasElapsed(actualTime, adsReadWdTimer, adsReadWdTime)) {
    adsReadWdTimer = actualTime;
    measureState = MeasureStates::REQUEST_ADC;
    return false;
  }
  return (ADS.getError() == ADS1X15_OK);
}

int16_t AdcReader::analogRead(Channel requestedChannel) {
  return adcValues[static_cast<uint8_t>(requestedChannel) & maxChannelNumber];
}

float AdcReader::voltageRead(Channel requestedChannel) {
  return ADS.toVoltage(adcValues[static_cast<uint8_t>(requestedChannel) & maxChannelNumber]);
}

void AdcReader::enableMqttSending(uint32_t interval) {
  mqttSendTime = interval > adsReadWdTime ? interval : adsReadWdTime;
  enableSending = true;
}

void AdcReader::disableMqttSending() {
  enableSending = false;
}

bool AdcReader::readyToRead() const {
  return valuesReady;
}

void AdcReader::intHandler() { // NOLINT(readability-convert-member-functions-to-static)
  adcReady = true;
}
