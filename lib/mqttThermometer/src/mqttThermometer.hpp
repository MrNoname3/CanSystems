#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection (MqttBase, HADiscovery).
#include "ds18b20Reader.hpp"                                        /// DS18B20 multi-sensor reader.
#include "common.hpp"                                               /// Time/Logger/Build/Str helpers.

/// @brief MQTT wrapper that periodically publishes DS18B20 readings, one sub-sub topic per sensor.
/// @details ESP8266 NONOS has no FreeRTOS tasks, so the work runs as a cooperative state machine in
/// `run()`: request a conversion, poll the conversion time non-blocking, then read and publish ONE
/// sensor per `run()` call (bounding the interrupt-disabling 1-Wire blocking to a single sensor at a
/// time), and finally wait `measurePeriod` until the next cycle. Nothing busy-waits.
///
/// Each reading is published as JSON on a sub-sub topic keyed by the sensor's 64-bit ROM id (e.g.
/// "temp/28ffaabbccddee01" -> {"tempC":23.50}); the ROM is a stable, globally unique identifier, so
/// topics and Home Assistant entities survive bus reordering. Each sensor is exposed as its own HA
/// device (via publishCanDeviceEntity), so no shared HADiscovery code needs changing.
/// @tparam MaxSensors Compile-time upper bound on the number of sensors.
template <uint8_t MaxSensors>
class MqttThermometer final : public MqttBase {
private:
  static constexpr uint8_t subSubTopicSize = 34U;                  // <subtopic up to 15> + '/' + 16 hex + null.
  static constexpr uint8_t payloadSize     = 24U;                  // {"tempC":-55.00} + margin.
  static constexpr uint8_t deviceIdSize    = 56U;                  // clientName + '_' + 16 hex + null.
  static constexpr uint8_t deviceNameSize  = 32U;                  // "DS18B20 " + 16 hex + null.
  static constexpr uint8_t swVersionSize   = 24U;                  // "65535 (deadbeef)" + margin.
  static constexpr float   minValidTempC   = -55.0F;               // Below the DS18B20 range -> invalid/disconnected.

  static constexpr const char PROGMEM entityName[]    = "Temperature";
  static constexpr const char PROGMEM entitySub[]     = "temperature";
  static constexpr const char PROGMEM valueTemplate[] = "{{ value_json.tempC }}";
  static constexpr const char PROGMEM unitDegC[]      = "\xc2\xb0\x43";       // UTF-8 "°C".
  static constexpr const char PROGMEM iconTherm[]     = "mdi:thermometer";
  static constexpr const char PROGMEM hwVersion[]     = "DS18B20";
  static constexpr const char PROGMEM payloadFmt[]    = R"({"tempC":%.2f})";
  static constexpr const char PROGMEM subSubFmt[]     = "%s/%s";             // <subtopic>/<rom>.
  static constexpr const char PROGMEM deviceIdFmt[]   = "%s_%s";             // <clientName>_<rom>.
  static constexpr const char PROGMEM deviceNameFmt[] = "DS18B20 %s";        // DS18B20 <rom>.
  static constexpr const char PROGMEM swVersionFmt[]  = "%hu (%08x)";

  /// @brief Cooperative measurement cycle: request -> wait conversion -> read each -> wait period.
  enum class State : uint8_t { Idle, Converting, Reading, Waiting };

public:
  /// @brief Constructs the thermometer handler.
  /// @param connectivity Reference to the Connectivity object managing MQTT.
  /// @param subtopic Base subtopic for readings (e.g. "temp"); sensors publish on "<subtopic>/<rom>".
  /// @param oneWirePin GPIO connected to the DS18B20 data line.
  /// @param measurePeriodMs Interval between measurement cycles, in milliseconds.
  MqttThermometer(Connectivity& connectivity, const char* subtopic, uint8_t oneWirePin, uint32_t measurePeriodMs) :
    MqttBase(connectivity, subtopic),
    reader(oneWirePin),
    measurePeriod(measurePeriodMs)
  {}

  /// @brief Default destructor.
  ~MqttThermometer() override = default;

  /// @brief Scans the 1-Wire bus and caches the sensor ROM addresses.
  /// @return `true` always; an empty bus is logged but does not block device boot.
  bool init() override {
    (void)reader.begin();
    Logger::get()->printf_P(PSTR("[TEMP] DS18B20 sensors found: %hhu\r\n"), reader.count());
    for(uint8_t i = 0U; i < reader.count(); ++i) {
      char rom[Ds18b20Reader<MaxSensors>::romHexSize] = {'\0'};
      if(reader.romHex(i, rom, sizeof(rom))) {
        Logger::get()->printf_P(PSTR("  %s\r\n"), rom);
      }
    }
    return true;
  }

  /// @brief Cooperative measurement state machine; advances one step per call. Never busy-waits.
  /// @return `true`.
  bool run() override {  // NOLINT(readability-function-cognitive-complexity) small, flat state machine
    if(reader.count() == 0U) { return true; }
    switch(state) {
      case State::Idle: {
        reader.requestConversion();                  // Non-blocking: sensors convert in the background.
        timer = millis();
        state = State::Converting;
      } break;
      case State::Converting: {
        if(Time::hasElapsed(millis(), timer, reader.conversionDelayMs())) {
          readIndex = 0U;
          state = State::Reading;
        }
      } break;
      case State::Reading: {
        readAndPublishOne(readIndex);                // One sensor per call: bounds the 1-Wire blocking.
        readIndex++;
        if(readIndex >= reader.count()) {
          timer = millis();
          state = State::Waiting;
        }
      } break;
      case State::Waiting: {
        if(Time::hasElapsed(millis(), timer, measurePeriod)) { state = State::Idle; }
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
  /// @brief Reads one sensor and publishes it; skips a disconnected/invalid sensor.
  void readAndPublishOne(uint8_t index) {
    const float tempC = reader.readTempC(index);
    if(tempC < minValidTempC) {
      char rom[Ds18b20Reader<MaxSensors>::romHexSize] = {'\0'};
      (void)reader.romHex(index, rom, sizeof(rom));
      Logger::get()->printf_P(PSTR("[TEMP] Sensor %hhu (%s) disconnected\r\n"), index, rom);
      return;
    }
    publishOne(index, tempC);
  }

  /// @brief Publishes one reading on its sub-sub topic.
  /// @param index Sensor index (for the cached ROM); @param tempC Temperature in degrees Celsius.
  void publishOne(uint8_t index, float tempC) {
    char rom[Ds18b20Reader<MaxSensors>::romHexSize] = {'\0'};
    if(!reader.romHex(index, rom, sizeof(rom))) { return; }
    char subSub[subSubTopicSize] = {'\0'};
    char payload[payloadSize] = {'\0'};
    const int32_t subLen = snprintf_P(subSub, sizeof(subSub), subSubFmt, getSubtopic(), rom);
    const int32_t payLen = snprintf_P(payload, sizeof(payload), payloadFmt, static_cast<double>(tempC));
    if((subLen <= 0) || (subLen >= static_cast<int32_t>(sizeof(subSub))) ||
       (payLen <= 0) || (payLen >= static_cast<int32_t>(sizeof(payload)))) { return; }
    (void)sendSubtopicMessage(subSub, payload);
  }

  /// @brief Publishes HA discovery for one sensor as a standalone HA device identified by its ROM.
  bool publishSensorDiscovery(uint8_t index) {
    char rom[Ds18b20Reader<MaxSensors>::romHexSize] = {'\0'};
    if(!reader.romHex(index, rom, sizeof(rom))) { return false; }

    char dataSub[subSubTopicSize]   = {'\0'};
    char deviceId[deviceIdSize]     = {'\0'};
    char deviceName[deviceNameSize] = {'\0'};
    char swVersion[swVersionSize]   = {'\0'};
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
  State    state     = State::Idle;                                 // Cooperative cycle state.
  uint32_t timer     = 0U;                                          // Shared timer for conversion / period waits.
  uint8_t  readIndex = 0U;                                          // Next sensor index to read in the Reading state.
};
