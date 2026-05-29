#pragma once
// ESP-only (OneWire + DallasTemperature). Guarded so non-ESP builds / native static analysis skip it.
#if defined(ESP8266) || defined(ESP32)
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <OneWire.h>                                                /// 1-Wire bus driver.
#include <DallasTemperature.h>                                      /// DS18B20 temperature sensor driver.
#include "common.hpp"                                               /// Common definitions and functions.

/// @brief Reads an arbitrary number of DS18B20 sensors on a single 1-Wire bus.
/// @details Scans the bus once at `begin()` and caches each sensor's 64-bit ROM address; the actual
/// sensor count is discovered at runtime and capped at the compile-time `MaxSensors`. Conversions are
/// non-blocking: call `requestConversion()`, wait `conversionDelayMs()` (polled by the owner), then
/// read with `readTempC()`. Transport-agnostic and shareable across ESP8266/ESP32.
/// @tparam MaxSensors Compile-time upper bound on the number of cached sensors.
template <uint8_t MaxSensors>
class Ds18b20Reader final {
  static_assert(MaxSensors > 0U && MaxSensors <= 16U, "MaxSensors must be between 1 and 16!");

public:
  static constexpr uint8_t romHexSize = 17U;                       // 16 hex chars (8-byte ROM) + null terminator.
  static constexpr float   invalidTempC = -127.0F;                 // Sentinel matching DEVICE_DISCONNECTED_C.

  /// @brief Constructs a reader for the given 1-Wire pin.
  /// @param oneWirePin GPIO connected to the DS18B20 data line (with a 4.7k pull-up).
  /// @param resolutionBits Conversion resolution (9..12 bits); higher = finer but slower.
  explicit Ds18b20Reader(uint8_t oneWirePin, uint8_t resolutionBits = 12U) :
    oneWire(oneWirePin),
    sensors(&oneWire),
    resolutionBits((resolutionBits < 9U) ? 9U : ((resolutionBits > 12U) ? 12U : resolutionBits))
  {}

  /// @brief Scans the bus and caches sensor addresses.
  /// @return `true` if at least one sensor was found.
  bool begin() {
    sensors.begin();
    sensors.setResolution(resolutionBits);
    sensors.setWaitForConversion(false);                           // Non-blocking: owner polls conversionDelayMs().
    const uint8_t found = sensors.getDeviceCount();
    sensorCount = (found > MaxSensors) ? MaxSensors : found;
    for(uint8_t i = 0U; i < sensorCount; ++i) {
      // cppcheck-suppress useStlAlgorithm
      sensors.getAddress(addresses[i], i);
    }
    return sensorCount > 0U;
  }

  /// @brief Number of sensors discovered on the bus (capped at MaxSensors).
  [[nodiscard]] uint8_t count() const { return sensorCount; }

  /// @brief Starts a temperature conversion on all sensors (non-blocking).
  void requestConversion() { sensors.requestTemperatures(); }

  /// @brief Worst-case conversion time for the configured resolution, in milliseconds.
  [[nodiscard]] uint16_t conversionDelayMs() const {
    return static_cast<uint16_t>(750U >> (12U - resolutionBits));  // 12-bit: 750ms, 11-bit: 375ms, ...
  }

  /// @brief Reads the last converted temperature of a sensor.
  /// @param index Sensor index (0..count()-1).
  /// @return Temperature in °C, or a value <= -55 °C if the sensor is invalid/disconnected.
  [[nodiscard]] float readTempC(uint8_t index) {
    if(index >= sensorCount) { return invalidTempC; }
    return sensors.getTempC(addresses[index]);
  }

  /// @brief Writes a sensor's ROM address as a lowercase hex string.
  /// @param index Sensor index (0..count()-1).
  /// @param buf Destination buffer (must be at least romHexSize bytes).
  /// @param bufSize Size of the destination buffer.
  /// @return `true` on success.
  [[nodiscard]] bool romHex(uint8_t index, char* buf, size_t bufSize) const {
    if((index >= sensorCount) || (buf == nullptr) || (bufSize < romHexSize)) { return false; }
    const uint8_t* a = addresses[index];
    const int32_t n = snprintf_P(buf, bufSize, PSTR("%02x%02x%02x%02x%02x%02x%02x%02x"),
                                 a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    return n == 16;
  }

  Ds18b20Reader(const Ds18b20Reader&) = delete;                     // Define copy constructor.
  Ds18b20Reader& operator=(const Ds18b20Reader&) = delete;          // Define copy assignment operator.
  Ds18b20Reader(Ds18b20Reader&&) = delete;                          // Define move constructor.
  Ds18b20Reader& operator=(Ds18b20Reader&&) = delete;               // Define move assignment operator.

private:
  OneWire oneWire;                                                  // 1-Wire bus instance.
  DallasTemperature sensors;                                        // DS18B20 driver bound to the bus.
  uint8_t resolutionBits;                                           // Conversion resolution (9..12).
  uint8_t sensorCount = 0U;                                         // Number of sensors discovered on the bus.
  DeviceAddress addresses[MaxSensors] = {};                         // Cached 8-byte ROM addresses.
};

#endif  // defined(ESP8266) || defined(ESP32)
