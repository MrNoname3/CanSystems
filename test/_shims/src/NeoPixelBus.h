#pragma once
// Minimal native-test shim for makuna/NeoPixelBus. RgbLedWrapper (and thus pumpControl /
// moistureReader) only needs the RgbColor(r,g,b) constructor plus Begin(), Show() and
// ClearTo(); the real driver is hardware-bound and does not build on the host. Everything
// here is a no-op so the dependent libraries link in native unit tests.
#include <stdint.h>

struct RgbColor {
  uint8_t R;
  uint8_t G;
  uint8_t B;
  RgbColor(uint8_t r, uint8_t g, uint8_t b) : R(r), G(g), B(b) {}
};

struct NeoGrbFeature {};
struct NeoWs2812xMethod {};

template <typename TFeature, typename TMethod>
class NeoPixelBus final {
public:
  NeoPixelBus(uint16_t /*countPixels*/, uint8_t /*pin*/) {}
  void Begin() {}                          // NOLINT(readability-convert-member-functions-to-static)
  void Show() {}                           // NOLINT(readability-convert-member-functions-to-static)
  void ClearTo(RgbColor /*color*/) {}      // NOLINT(readability-convert-member-functions-to-static)
};
