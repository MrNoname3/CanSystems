#include "pumpControl.hpp"
#include <Arduino.h>
#include "common.hpp"

volatile uint16_t PumpControl::flowCounter = 0U;

PumpControl::PumpControl(PCF8574& pcf8574, uint8_t pwmPin, uint8_t intPin, uint8_t currentSensePin, void (*reportError)(uint8_t errCode)) :
  pcf(pcf8574),
  pwmPin(pwmPin),
  intPin(intPin),
  currentSensePin(currentSensePin),
  prevFlowCounter(0U),
  irrigationQueue(),
  irrigationState(IrrigationState::CALIBRATION),
  analogValue(0U),
  eventTimer(0U),
  errorCheckTimer(0U),
  error(0U),
  reportError(reportError),
  limitSwitches{nullptr},
  calibrationValue(0U)
{
  pinMode(pwmPin, OUTPUT);
  pinMode(intPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(intPin), irqHandler, FALLING);
}

void PumpControl::init() {
  eventTimer = millis();
}

void PumpControl::run() {
  filterAnalogValue();
  switch(irrigationState) {
    case IrrigationState::IDLE: {
      if(!irrigationQueue.isEmpty()) {
        const bool chSelectionSuccess = selectChannel(irrigationQueue.peek().channel);
        if(chSelectionSuccess) {
          analogWrite(pwmPin, irrigationQueue.peek().pwmValue);
          eventTimer = millis();
          errorCheckTimer = millis();
          prevFlowCounter = flowCounter = 0U;
          irrigationState = IrrigationState::RUN;
        } else {
          setError(ERROR::CH_SELECT);
          irrigationState = IrrigationState::ERROR;
        }
      } else {
        if(flowCounter > 0U) {
          setError(ERROR::FLOW_OVERRUN);
          irrigationState = IrrigationState::ERROR;
        }
        if(abs(calculateCurrent()) > maxAllowedStandbyCurrent) {
          setError(ERROR::PUMP_OVERRUN);
          irrigationState = IrrigationState::ERROR;
        }
      }
      if(error > 0U && reportError != nullptr) {
        reportError(getError());
      }
    } break;
    case IrrigationState::RUN: {
      const uint8_t actualCh = irrigationQueue.peek().channel;
      const bool limitSwitchReached = (limitSwitches[actualCh] != nullptr) ? limitSwitches[actualCh]() : false;
      if((millis() - eventTimer > TimeConverter::minToMs(irrigationQueue.peek().duration)) || limitSwitchReached) {
        prevFlowCounter = flowCounter = 0U;
        irrigationState = IrrigationState::STOP;
      } else {
        if(millis() - errorCheckTimer > errorCheckTime) {
          errorCheckTimer = millis();
          if(static_cast<bool>(irrigationQueue.peek().checkFlow)) {
            const bool flowCheckSuccess = flowCounter > prevFlowCounter;
            if(flowCheckSuccess) {
              prevFlowCounter = flowCounter;
            } else {
              setError(ERROR::FLOW_STUCK);
              irrigationState = IrrigationState::ERROR;
            }
          }
          if(static_cast<bool>(irrigationQueue.peek().checkCurrent)) {
            const uint16_t actualCurrent = abs(calculateCurrent());
            if(actualCurrent < maxAllowedStandbyCurrent) {
              setError(ERROR::PUMP_UC);
              irrigationState = IrrigationState::ERROR;
            }
            // if(actualCurrent > ?) {
            //   setError(ERROR::PUMP_OC);
            //   irrigationState = IrrigationState::ERROR;
            // }
          }
        }
      }
    } break;
    case IrrigationState::STOP: {
      IrrigationQueueElement actualElement = irrigationQueue.pop();
      if(actualElement.repeatNum > 0U) {
        actualElement.repeatNum--;
        createIrrigation(actualElement);
      }
      if(irrigationQueue.isEmpty()) {
        digitalWrite(pwmPin, LOW);
      } else {
        if(actualElement.channel != irrigationQueue.peek().channel) {
          digitalWrite(pwmPin, LOW);
        }
      }
      irrigationState = IrrigationState::IDLE;
    } break;
    case IrrigationState::ERROR: {
      digitalWrite(pwmPin, LOW);
      if(!irrigationQueue.isEmpty()) {
        irrigationQueue.pop();
      }
      irrigationState = IrrigationState::IDLE;
    } break;
    case IrrigationState::CALIBRATION: {
      if(millis() - eventTimer > TimeConverter::secToMs(5U)) {
        const int16_t calValue = 511 - static_cast<int16_t>(analogValue);
        if(static_cast<uint16_t>(abs(calValue)) < 20U) {
          calibrationValue = calValue;
        }
        irrigationState = IrrigationState::IDLE;
      }
    } break;
  };
}

void PumpControl::createIrrigation(uint8_t irrigationInfo, uint8_t pwmValue, uint8_t repeatNum) {
  if(!irrigationQueue.isFull()) {
    irrigationQueue.put(IrrigationQueueElement(irrigationInfo, pwmValue, repeatNum));
  } else {
    setError(ERROR::QUEUE_FULL);
  }
}

void PumpControl::createIrrigation(uint8_t channel, uint8_t duration, bool checkFlow, bool checkCurrent, uint8_t pwmValue, uint8_t repeatNum) {
  if(!irrigationQueue.isFull()) {
    irrigationQueue.put(IrrigationQueueElement(channel, duration, checkFlow, checkCurrent, pwmValue, repeatNum));
  } else {
    setError(ERROR::QUEUE_FULL);
  }
}

void PumpControl::createIrrigation(IrrigationQueueElement irrigationElement) {
  if(!irrigationQueue.isFull()) {
    irrigationQueue.put(irrigationElement);
  } else {
    setError(ERROR::QUEUE_FULL);
  }
}

int16_t PumpControl::calculateCurrent() const {
  static constexpr uint16_t voltageReference = 5000U;     // 5V reference in millivolts.
  static constexpr uint16_t adcResolution = 1024U;        // 10-bit ADC resolution.
  static constexpr int16_t zeroCurrentOffset = 2500;      // 2.5V offset at 0A.
  static constexpr uint16_t sensorSensitivity = 185U;     // 185mV / 1A for ACS712-5A.

  // Calculate the sensor output voltage in millivolts from the analog value.
  const int32_t sensorVoltageMV = (int32_t(analogValue) * voltageReference) / adcResolution;
  // Calculate the current in milliamps (mA). (sensorVoltageMV - zeroCurrentOffset) gives the voltage deviation from 0A.
  const int32_t currentMA = ((sensorVoltageMV - zeroCurrentOffset) * 1000U) / sensorSensitivity;
  return static_cast<int16_t>(currentMA);
}

void PumpControl::irqHandler() {
  flowCounter++;
}

bool PumpControl::selectChannel(uint8_t channel) const {
  const uint8_t actualRegValue = pcf.getRegisterValue();
  uint8_t newRegValue = actualRegValue & 0xF0;            // Keep high 4 bits, clear low 4 bits.
  newRegValue |= (1U << channel);                         // Apply the new channel selection.
  return pcf.write(newRegValue);
}

void PumpControl::filterAnalogValue() {
  // Complement filter calculation.
  static constexpr uint8_t adcInputFilterAlpha = 10U;     // Complement filter ALPHA value.
  const uint16_t rawAnalogValue = static_cast<uint16_t>(analogRead(currentSensePin) + calibrationValue);
  analogValue = ((adcInputFilterAlpha * rawAnalogValue) + (100U - adcInputFilterAlpha) * (uint32_t)analogValue) / 100U;
}

void PumpControl::setError(ERROR err) {
  if(err == ERROR::NONE) {
    this->error = 0U;
  } else {
    this->error |= static_cast<uint8_t>(err);
  }
}

const uint8_t PumpControl::getError() {
  const uint8_t err = error;
  error = 0U;
  return err;
}

void PumpControl::addLimitSwitch(uint8_t channel, bool (*limitSwitch)()) {
  channel &= channelSafetyMask;
  limitSwitches[channel] = limitSwitch;
}

void PumpControl::skipActualIrrigation() {
  if(irrigationState == IrrigationState::RUN) {
    eventTimer = millis() - TimeConverter::minToMs(irrigationQueue.peek().duration);
  }
}

void PumpControl::skipAllIrrigations() {
  irrigationQueue.clear();
  irrigationState = IrrigationState::ERROR;
}