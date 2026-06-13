#pragma once
// Native-test shim for Ds18b20Reader: the real one is ESP-only (OneWire + DallasTemperature),
// so the lib is lib_ignore'd for native_test and this API mirror stands in. The 1-Wire bus is
// faked through static test hooks; ROM ids are synthesized deterministically from the index.
// The ESP builds compile mqttThermometer against the real class, so any signature divergence
// here surfaces as a native-only compile error.
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

template <uint8_t MaxSensors>
class Ds18b20Reader final {
  static_assert(MaxSensors > 0U && MaxSensors <= 16U, "MaxSensors must be between 1 and 16!");

public:
  static constexpr uint8_t romHexSize = 17U;                       // 16 hex chars (8-byte ROM) + null terminator.
  static constexpr float   invalidTempC = -127.0F;                 // Sentinel matching DEVICE_DISCONNECTED_C.

  explicit Ds18b20Reader(uint8_t /*oneWirePin*/, uint8_t /*resolutionBits*/ = 12U) {}

  bool begin() {
    sensorCount = (fakeSensorCount > MaxSensors) ? MaxSensors : fakeSensorCount;
    return sensorCount > 0U;
  }

  [[nodiscard]] uint8_t count() const { return sensorCount; }

  void requestConversion() { ++requestCount; }                     // NOLINT(readability-convert-member-functions-to-static)

  [[nodiscard]] uint16_t conversionDelayMs() const { return 750U; }  // NOLINT(readability-convert-member-functions-to-static)

  [[nodiscard]] float readTempC(uint8_t index) {                   // NOLINT(readability-convert-member-functions-to-static)
    if(index >= sensorCount) { return invalidTempC; }
    return fakeTempsC[index];
  }

  // NOLINTNEXTLINE(readability-non-const-parameter) snprintf writes through buf; mirrors the real signature
  [[nodiscard]] bool romHex(uint8_t index, char* buf, size_t bufSize) const {
    if((index >= sensorCount) || (buf == nullptr) || (bufSize < romHexSize)) { return false; }
    const int written = snprintf(buf, bufSize, "28ff0000000000%02x", index);
    return written == 16;
  }

  // ---- test hooks (static so tests can set them without a handle) ----
  static inline uint8_t fakeSensorCount = 0U;                      // Sensors "found" by the next begin().
  static inline float   fakeTempsC[MaxSensors] = {};               // Per-sensor temperature returned by readTempC().
  static inline int     requestCount = 0;                          // Number of requestConversion() calls.
  static void resetState() {
    fakeSensorCount = 0U;
    for(float& temp : fakeTempsC) { temp = 0.0F; }
    requestCount = 0;
  }

private:
  uint8_t sensorCount = 0U;
};
