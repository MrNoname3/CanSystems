#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "ds18b20Reader.hpp"                                        /// DS18B20 multi-sensor reader.

/// @brief MQTT wrapper that periodically publishes DS18B20 readings, one sub-sub topic per sensor.
/// @details Sits on top of a `Ds18b20Reader` and, on a timer, runs a non-blocking
/// request -> wait -> publish cycle. Each sensor's reading is published as JSON on a sub-sub topic
/// keyed by the sensor's 64-bit ROM id (e.g. "temp/28ffaabbccddee01" -> {"tempC":23.50}); the ROM is
/// a stable, globally unique identifier, so topics and Home Assistant entities survive reordering on
/// the bus. Each sensor is exposed as its own HA device (via `publishCanDeviceEntity`), so no shared
/// HADiscovery code needs changing.
/// @tparam MaxSensors Compile-time upper bound on the number of sensors.
template <uint8_t MaxSensors>
class MqttThermometer final : public MqttBase {
private:
  static constexpr uint8_t subSubTopicSize = 34U;                  // <subtopic up to 15> + '/' + 16 hex + null.
  static constexpr uint8_t payloadSize     = 24U;                  // {"tempC":-55.00} + margin.
  static constexpr uint8_t deviceIdSize    = 56U;                  // clientName + '_' + 16 hex + null.
  static constexpr uint8_t deviceNameSize  = 32U;                  // "DS18B20 " + 16 hex + null.
  static constexpr uint8_t swVersionSize   = 24U;                  // "65535 (deadbeef)" + margin.

  static constexpr const char PROGMEM entityName[]   = "Temperature";
  static constexpr const char PROGMEM entitySub[]    = "temperature";
  static constexpr const char PROGMEM valueTemplate[] = "{{ value_json.tempC }}";
  static constexpr const char PROGMEM unitDegC[]     = "\xc2\xb0\x43";        // UTF-8 "°C".
  static constexpr const char PROGMEM iconTherm[]    = "mdi:thermometer";
  static constexpr const char PROGMEM hwVersion[]    = "DS18B20";
  static constexpr const char PROGMEM payloadFmt[]   = R"({"tempC":%.2f})";
  static constexpr const char PROGMEM subSubFmt[]    = "%s/%s";              // <subtopic>/<rom>.
  static constexpr const char PROGMEM deviceIdFmt[]  = "%s_%s";              // <clientName>_<rom>.
  static constexpr const char PROGMEM deviceNameFmt[] = "DS18B20 %s";        // DS18B20 <rom>.
  static constexpr const char PROGMEM swVersionFmt[] = "%hu (%08x)";

  /// @brief Non-blocking measurement state.
  enum class State : uint8_t {
    IDLE = 0U,            // Waiting for the next measurement period.
    CONVERTING,           // Conversion requested; waiting for it to finish.
    PUBLISH               // Conversion done; publish all readings.
  };

public:
  /// @brief Constructs the thermometer handler.
  /// @param connectivity Reference to the Connectivity object managing MQTT.
  /// @param subtopic Base subtopic for readings (e.g. "temp"); sensors publish on "<subtopic>/<rom>".
  /// @param oneWirePin GPIO connected to the DS18B20 data line.
  /// @param measurePeriodMs Interval between measurement cycles, in milliseconds.
  MqttThermometer(Connectivity& connectivity, const char* subtopic, uint8_t oneWirePin, uint32_t measurePeriodMs) :
    MqttBase(connectivity, subtopic),
    reader(oneWirePin),
    measurePeriod(measurePeriodMs),
    eventTimer(0U),
    convTimer(0U),
    state(State::IDLE)
  {}

  /// @brief Default destructor.
  ~MqttThermometer() override = default;

  /// @brief Scans the 1-Wire bus.
  /// @return `true` always; a bus with no sensors is logged but does not block device boot.
  bool init() override {
    const bool found = reader.begin();
    Logger::get().printf_P(PSTR("[TEMP] DS18B20 sensors found: %hhu\r\n"), reader.count());
    (void)found;
    eventTimer = millis();
    return true;
  }

  /// @brief Drives the non-blocking measure/publish cycle.
  /// @return `true`.
  bool run() override {
    const uint32_t now = millis();
    switch(state) {
      case State::IDLE: {
        if((reader.count() > 0U) && Time::hasElapsed(now, eventTimer, measurePeriod)) {
          eventTimer = now;
          reader.requestConversion();
          convTimer = now;
          state = State::CONVERTING;
        }
      } break;
      case State::CONVERTING: {
        if(Time::hasElapsed(now, convTimer, reader.conversionDelayMs())) {
          state = State::PUBLISH;
        }
      } break;
      case State::PUBLISH: {
        publishReadings();
        state = State::IDLE;
      } break;
    }
    return true;
  }

  /// @brief Publishes a HA discovery config for every sensor (each as its own HA device).
  /// @return `true` if all sensors published successfully.
  bool publishDiscovery() override {
    bool allOk = true;
    for(uint8_t i = 0U; i < reader.count(); ++i) {
      allOk = publishSensorDiscovery(i) && allOk;
    }
    return allOk;
  }

  /// @brief Read-only sensor: incoming messages are ignored.
  void messageArrivedCallback(JsonDocument& payloadJson) override { (void)payloadJson; }

  MqttThermometer(const MqttThermometer&) = delete;                 // Define copy constructor.
  MqttThermometer& operator=(const MqttThermometer&) = delete;      // Define copy assignment operator.
  MqttThermometer(MqttThermometer&&) = delete;                      // Define move constructor.
  MqttThermometer& operator=(MqttThermometer&&) = delete;           // Define move assignment operator.

private:
  /// @brief Publishes the current reading of every sensor on its sub-sub topic.
  void publishReadings() {
    for(uint8_t i = 0U; i < reader.count(); ++i) {
      char rom[Ds18b20Reader<MaxSensors>::romHexSize] = {'\0'};
      if(!reader.romHex(i, rom, sizeof(rom))) { continue; }
      const float tempC = reader.readTempC(i);
      if(tempC < -55.0F) {                                          // Below DS18B20 range -> invalid/disconnected.
        Logger::get().printf_P(PSTR("[TEMP] Sensor %s disconnected\r\n"), rom);
        continue;
      }
      char subSub[subSubTopicSize] = {'\0'};
      char payload[payloadSize] = {'\0'};
      const int32_t subLen = snprintf_P(subSub, sizeof(subSub), subSubFmt, getSubtopic(), rom);
      const int32_t payLen = snprintf_P(payload, sizeof(payload), payloadFmt, static_cast<double>(tempC));
      if((subLen <= 0) || (subLen >= static_cast<int32_t>(sizeof(subSub))) ||
         (payLen <= 0) || (payLen >= static_cast<int32_t>(sizeof(payload)))) { continue; }
      (void)sendSubtopicMessage(subSub, payload);
    }
  }

  /// @brief Publishes HA discovery for one sensor as a standalone HA device identified by its ROM.
  bool publishSensorDiscovery(uint8_t index) {
    char rom[Ds18b20Reader<MaxSensors>::romHexSize] = {'\0'};
    if(!reader.romHex(index, rom, sizeof(rom))) { return false; }

    char dataSub[subSubTopicSize]     = {'\0'};
    char deviceId[deviceIdSize]       = {'\0'};
    char deviceName[deviceNameSize]   = {'\0'};
    char swVersion[swVersionSize]     = {'\0'};
    (void)snprintf_P(dataSub,    sizeof(dataSub),    subSubFmt,     getSubtopic(), rom);
    (void)snprintf_P(deviceId,   sizeof(deviceId),   deviceIdFmt,   getClientNameStr(), rom);
    (void)snprintf_P(deviceName, sizeof(deviceName), deviceNameFmt, rom);
    (void)snprintf_P(swVersion,  sizeof(swVersion),  swVersionFmt,  Build::getFwVersion(), Build::getGitHash());

    using HA = Connectivity::HADiscovery;
    const HA::EntityConfig config = HA::EntityConfig::sensor(
      entityName, valueTemplate, unitDegC, HA::StateClass::measurement, HA::DeviceClass::temperature, iconTherm);
    HA::CanDeviceConfig devConfig{};
    devConfig.deviceId            = deviceId;
    devConfig.deviceName          = deviceName;
    devConfig.swVersion           = swVersion;
    devConfig.extraAvailTopic     = dataSub;          // Unused when skipCanAvailability is true; must be non-null.
    devConfig.dataSubtopic        = dataSub;
    devConfig.hwVersion           = hwVersion;
    devConfig.skipCanAvailability = true;             // Follow the ESP node's availability (no per-probe LWT).
    return doPublishCanDeviceEntityDiscovery(entitySub, config, devConfig);
  }

  Ds18b20Reader<MaxSensors> reader;                                 // The underlying multi-sensor reader.
  uint32_t measurePeriod;                                           // Interval between measurement cycles.
  uint32_t eventTimer;                                              // Timer for the measurement period.
  uint32_t convTimer;                                               // Timer for the conversion wait.
  State state;                                                      // Current measurement state.
};
