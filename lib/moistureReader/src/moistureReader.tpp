template<uint8_t N>
MoistureReader<N>::MoistureReader(const Multiplexer& multiplexer, const uint8_t (&channels)[N], uint32_t readTime, void (*dataSender)(const uint8_t (&data)[8])) :
  multiplexer(multiplexer),
  channels(channels),
  readTime(readTime),
  readTimer(0UL),
  dataSender(dataSender),
  sensorWakeupTimer(0UL),
  readState(ReadState::IDLE),
  readIndex(0U),
  filteringTimer(0UL),
  moistureValue(0U)
{

}

template<uint8_t N>
void MoistureReader<N>::init() {
  readTimer = millis();
}

template<uint8_t N>
void MoistureReader<N>::run() {
  switch(readState) {
    case ReadState::IDLE: {
      if(millis() - readTimer > readTime) {
        readTimer = millis();
        multiplexer.enableRead();
        sensorWakeupTimer = millis();
        readState = ReadState::WAKEUP;
      }
    } break;
    case ReadState::WAKEUP: {
      if(millis() - sensorWakeupTimer > sensorWakeupTime) {
        readState = ReadState::SETUP;
      }
    } break;
    case ReadState::SETUP: {
        multiplexer.selectChannel(channels[readIndex]);
        filteringTimer = millis();
        moistureValue = 0U;
        readState = ReadState::READING;
    } break;
    case ReadState::READING: {
      filterAnalogValue();
      if(millis() - filteringTimer > filteringTime) {
        if(dataSender != nullptr) {
          const uint8_t moistureH = static_cast<uint8_t>((moistureValue >> 0) & 0xFF);
          const uint8_t moistureL = static_cast<uint8_t>((moistureValue >> 8) & 0xFF);
          dataSender({channels[readIndex], moistureH, moistureL, 0U, 0U, 0U, 0U, 0U});
        }
        readIndex++;
        if(readIndex >= channelNum) {
          readIndex = 0U;
          multiplexer.disableRead();
          readState = ReadState::IDLE;
        } else {
          readState = ReadState::SETUP;
        }
      }
    } break;
  };
}

template<uint8_t N>
void MoistureReader<N>::triggerImmediateMeasurement() {
  readTimer = millis() - readTime;
}

template<uint8_t N>
void MoistureReader<N>::filterAnalogValue() {
  // Complement filter calculation.
  static constexpr uint8_t adcInputFilterAlpha = 10U;     // Complement filter ALPHA value.
  const uint16_t rawAnalogValue = multiplexer.analogReadAdvanced();
  moistureValue = ((adcInputFilterAlpha * rawAnalogValue) + (100U - adcInputFilterAlpha) * (uint32_t)moistureValue) / 100U;
}