#include "radiation.hpp"
#include "configHandler.hpp"                                        /// Read tube type from /config/tube.json.
#include <cmath>                                                    /// lroundf for correct float-to-integer rounding.

volatile uint32_t Radiation::cpm = 0U;
volatile bool Radiation::measureDone = false;
volatile uint32_t Radiation::cpmToSend = 0U;

Radiation::Radiation(Connectivity& connectivity, const char* subtopic, uint8_t sensorPin) :
  MqttBase(connectivity, subtopic),
  measureTicker(),
  sensorPin(sensorPin) {
  pinMode(sensorPin, INPUT);
}

bool Radiation::publishDiscovery() { // NOLINT(readability-convert-member-functions-to-static)
  using HA = Connectivity::HADiscovery;
  const HA::EntityConfig config = HA::EntityConfig::sensor(
      PSTR("Radiation"), PSTR("{{ value_json.tick }}"), PSTR("CPM"),
      HA::StateClass::measurement, HA::DeviceClass::none, PSTR("mdi:radioactive"));
  return doPublishEntityDiscovery(config);
}

Radiation::TubeType Radiation::loadTubeType() {
  uint8_t tubeValue = 0U;
  if(!ConfigHandler::getJsonValue(FileName::getTubeConfigLocation(), PSTR("tube"), tubeValue)) {
    return TubeType::Unknown;
  }
  const TubeType t = static_cast<TubeType>(tubeValue);
  switch(t) {
    case TubeType::J305:
    case TubeType::M4011:
      return t;
    default:
      Logger::get()->printf_P(PSTR("[RAD] Unknown tube value: %hhu\r\n"), tubeValue);
      return TubeType::Unknown;
  }
}

bool Radiation::init() { // NOLINT(readability-convert-member-functions-to-static,readability-make-member-function-const)
  attachInterrupt(digitalPinToInterrupt(sensorPin), counter, FALLING);
  measureTicker.attach_ms(measureTime, measure);
  cpm = 0U;
  tubeType = loadTubeType();
  Logger::get()->printf_P(PSTR("[RAD] Tube type: %hhu\r\n"), static_cast<uint8_t>(tubeType));
  return true;
}

void Radiation::end() { // NOLINT(readability-make-member-function-const)
  detachInterrupt(digitalPinToInterrupt(sensorPin));
  measureTicker.detach();
  cpm = 0U;
  measureDone = false;
}

bool Radiation::run() { // NOLINT(readability-convert-member-functions-to-static)
  if(measureDone) {
    measureDone = false;
    char dataOut[dataOutBufSize] = { '\0' };
    int32_t dataOutSize;

    const float factor = getTubeFactor(tubeType);
    // Scale CPM/factor by 10000 for 4-decimal fixed-point; 0 when tube type is unknown.
    // radian = sievert * 100 shares the same integer (sievert*10000 / 100 = radian*100).
    const uint32_t sX10k = (factor > 0.0F) ? static_cast<uint32_t>(lroundf(static_cast<float>(cpmToSend) / factor * 10000.0F)) : 0U;
    dataOutSize = snprintf_P(dataOut, sizeof(dataOut), fullMessageFrame,
                             cpmToSend,
                             sX10k / 10000U, sX10k % 10000U,   // sievert: whole + 4-digit frac
                             sX10k / 100U, sX10k % 100U);    // radian:  whole + 2-digit frac

    const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
    if(!dataOutValid) { return false; }
    if(!MqttBase::sendMessage(dataOut)) { return false; }
  }
  return true;
}

void Radiation::counter() { // NOLINT(readability-convert-member-functions-to-static)
  cpm++;
}

void Radiation::measure() { // NOLINT(readability-convert-member-functions-to-static)
  // The pin ISR can preempt this Ticker callback; without masking, a pulse landing between the
  // snapshot and the clear would be wiped. A pulse during the masked window stays latched in the
  // interrupt controller and is counted right after, in the next measurement period.
  noInterrupts();
  cpmToSend = cpm;
  cpm = 0U;
  interrupts();
  measureDone = true;
}
