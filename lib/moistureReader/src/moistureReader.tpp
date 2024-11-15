template<uint8_t N>
MoistureReader<N>::MoistureReader(const Multiplexer& multiplexer, const uint8_t (&channels)[N], uint32_t readTime, void (*dataSender)(const uint8_t (&data)[8])) :
  multiplexer(multiplexer),
  channels(channels),
  readTime(readTime),
  eventTimer(0UL),
  dataSender(dataSender),
  readState(ReadState::IDLE),
  readIndex(0U),
  moistureValue(0U)
{}

template<uint8_t N>
void MoistureReader<N>::init() {
  eventTimer = millis();
}

template<uint8_t N>
void MoistureReader<N>::run() {
  const uint32_t actualTime = millis();
  switch(readState) {
    case ReadState::IDLE: {
      if(actualTime - eventTimer > readTime) {
        eventTimer = actualTime;
        multiplexer.enableRead();
        readState = ReadState::WAKEUP;
      }
    } break;
    case ReadState::WAKEUP: {
      if(actualTime - eventTimer > sensorWakeupTime) {
        readState = ReadState::SETUP;
      }
    } break;
    case ReadState::SETUP: {
        multiplexer.selectChannel(channels[readIndex]);
        eventTimer = actualTime;
        moistureValue = 0U;
        readState = ReadState::READING;
    } break;
    case ReadState::READING: {
      filterAnalogValue();
      if(actualTime - eventTimer > filteringTime) {
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
  if(readState == ReadState::IDLE) {
    eventTimer = millis() - readTime;
  }
}

template<uint8_t N>
void MoistureReader<N>::filterAnalogValue() {
  // Complement filter calculation.
  static constexpr uint8_t adcInputFilterAlpha = 10U;     // Complement filter ALPHA value.
  const uint16_t rawAnalogValue = multiplexer.analogReadAdvanced();
  moistureValue = ((adcInputFilterAlpha * rawAnalogValue) + (100U - adcInputFilterAlpha) * (uint32_t)moistureValue) / 100U;
}