#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "ds18b20Reader.hpp"                                        /// DS18B20 multi-sensor reader.
#include "freertos/FreeRTOS.h"                                      /// FreeRTOS base.
#include "freertos/task.h"                                          /// Bus task creation / vTaskDelay.
#include "freertos/queue.h"                                         /// Readings hand-off queue.

/// @brief MQTT wrapper that periodically publishes DS18B20 readings, one sub-sub topic per sensor.
/// @details The blocking 1-Wire bus work runs in its own FreeRTOS task: it requests a conversion,
/// sleeps the conversion time with vTaskDelay, reads every sensor, and pushes the values into a
/// FreeRTOS queue — then sleeps `measurePeriodMs` until the next cycle. The publish side runs in the
/// cooperative loop (`run()`), draining the queue and publishing each reading; this keeps all MQTT
/// access on the network task (sole PubSubClient owner) while the bus blocking is isolated.
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

  // Bus task configuration.
  static constexpr uint32_t taskStackSize  = 4096U;                // Stack bytes for the bus task.
  static constexpr uint8_t  taskPriority   = 1U;                   // Same as the Arduino loop task.
  static constexpr uint8_t  taskCore       = 1U;                   // APP_CPU (the Arduino loop core); the task mostly sleeps.

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

  /// @brief One sensor reading handed from the bus task to the publish side.
  struct Reading {
    uint8_t index;     // Sensor index into the reader's cached ROM list.
    float   tempC;     // Temperature in degrees Celsius (already range-checked).
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
    readingsQueue(nullptr),
    busTaskHandle(nullptr)
  {}

  /// @brief Default destructor.
  ~MqttThermometer() override = default;

  /// @brief Scans the 1-Wire bus and spawns the bus task if any sensor was found.
  /// @return `true` always; an empty bus is logged but does not block device boot.
  bool init() override {
    (void)reader.begin();
    Logger::get().printf_P(PSTR("[TEMP] DS18B20 sensors found: %hhu\r\n"), reader.count());
    if(reader.count() == 0U) { return true; }

    readingsQueue = xQueueCreate(MaxSensors, sizeof(Reading));
    if(readingsQueue == nullptr) {
      Logger::get().printf_P(PSTR("[TEMP] Readings queue creation failed!\r\n"));
      return true;
    }
    const BaseType_t created = xTaskCreatePinnedToCore(
      busTask, "owBus", taskStackSize, this, taskPriority, &busTaskHandle, taskCore);
    if(created != pdPASS) {
      Logger::get().printf_P(PSTR("[TEMP] Bus task creation failed!\r\n"));
    }
    return true;
  }

  /// @brief Publish side (cooperative loop): drains queued readings and publishes them.
  /// @return `true`.
  bool run() override {
    if(readingsQueue == nullptr) { return true; }
    Reading reading{};
    while(xQueueReceive(readingsQueue, &reading, 0) == pdTRUE) {
      publishOne(reading.index, reading.tempC);
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
  /// @brief Bus task: convert -> wait -> read all sensors -> enqueue valid readings -> sleep, forever.
  /// Owns all blocking 1-Wire I/O; the publish side only reads the (immutable-after-scan) ROM list.
  static void busTask(void* arg) {
    MqttThermometer* const self = static_cast<MqttThermometer*>(arg);
    for(;;) {
      self->reader.requestConversion();
      vTaskDelay(pdMS_TO_TICKS(self->reader.conversionDelayMs()));
      const uint8_t count = self->reader.count();
      for(uint8_t i = 0U; i < count; ++i) {
        const float tempC = self->reader.readTempC(i);
        if(tempC < minValidTempC) {
          Logger::get().printf_P(PSTR("[TEMP] Sensor %hhu disconnected\r\n"), i);
          continue;
        }
        const Reading reading{i, tempC};
        (void)xQueueSend(self->readingsQueue, &reading, 0);  // Drop if the publish side is behind; next cycle resends.
      }
      vTaskDelay(pdMS_TO_TICKS(self->measurePeriod));        // Sleep until the next cycle (no CPU used).
    }
  }

  /// @brief Publishes one reading on its sub-sub topic. Runs on the network/loop task.
  /// @param index Sensor index (for the cached ROM); @param tempC Temperature from the bus task.
  void publishOne(uint8_t index, float tempC) {
    char rom[Ds18b20Reader<MaxSensors>::romHexSize] = {'\0'};
    if(!reader.romHex(index, rom, sizeof(rom))) { return; }      // romHex reads the immutable ROM cache (safe).
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

  Ds18b20Reader<MaxSensors> reader;                                 // The underlying multi-sensor reader (bus task owns the I/O).
  uint32_t measurePeriod;                                           // Interval between measurement cycles.
  QueueHandle_t readingsQueue;                                      // Bus task -> publish side hand-off.
  TaskHandle_t busTaskHandle;                                       // Handle of the spawned bus task.
};
