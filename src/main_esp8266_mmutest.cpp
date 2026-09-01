//--- Headers ---//
// Bench firmware for the ESP8266 instruction cache. Times two flash-resident workloads: a large
// one whose code does not fit a 16 KB icache but does fit a 32 KB one, and a small one that fits
// either way. The gap between them is what the MMU setting costs or saves.
//
//   pio run -e esp8266_mmutest -t upload --upload-port /dev/ttyUSB0
//   PLATFORMIO_BUILD_FLAGS="-D PIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48" pio run -e esp8266_mmutest -t upload --upload-port /dev/ttyUSB0
//   pio device monitor -p /dev/ttyUSB0 -b 115200
#include <Arduino.h>          /// Arduino libraries header.

//--- Workload ---//
// Each step is a data-dependent mix, so the chain cannot be folded away; the repetition is what
// makes one function large enough that thirty-two of them overflow a 16 KB cache.
namespace {
  // clang-format off
  #define MIX1(k)  v = (v * 1664525UL) + (1013904223UL + (k)); v ^= (v >> 13U);
  #define MIX4(k)  MIX1(k) MIX1((k) + 1U) MIX1((k) + 2U) MIX1((k) + 3U)
  #define MIX16(k) MIX4(k) MIX4((k) + 4U) MIX4((k) + 8U) MIX4((k) + 12U)
  #define MIX64(k) MIX16(k) MIX16((k) + 16U) MIX16((k) + 32U) MIX16((k) + 48U)
  #define STEP(n)  uint32_t __attribute__((noinline)) step##n(uint32_t v) { MIX64((n) * 64UL) return v; }

  STEP(0)  STEP(1)  STEP(2)  STEP(3)  STEP(4)  STEP(5)  STEP(6)  STEP(7)
  STEP(8)  STEP(9)  STEP(10) STEP(11) STEP(12) STEP(13) STEP(14) STEP(15)
  STEP(16) STEP(17) STEP(18) STEP(19) STEP(20) STEP(21) STEP(22) STEP(23)

  using StepFn = uint32_t (*)(uint32_t);
  StepFn steps[] = {
    step0,  step1,  step2,  step3,  step4,  step5,  step6,  step7,
    step8,  step9,  step10, step11, step12, step13, step14, step15,
    step16, step17, step18, step19, step20, step21, step22, step23
  };
  // clang-format on

  constexpr uint8_t smallSetSize = 4U;      // A few kilobytes of code: fits either cache size.
  constexpr uint16_t passes = 400U;         // Times the whole set is walked in one measurement.
  constexpr uint8_t rounds = 7U;            // Measurements taken; the shortest one is reported.

  volatile uint32_t sink = 0U;              // Keeps the result live so the walk is not elided.

  uint32_t walk(uint8_t setSize) {
    uint32_t v = sink;
    for(uint16_t p = 0U; p < passes; p++) {
      for(uint8_t i = 0U; i < setSize; i++) { v = steps[i](v); }
    }
    sink = v;
    return v;
  }

  uint32_t timeWalk(uint8_t setSize) {
    uint32_t best = UINT32_MAX;
    for(uint8_t r = 0U; r < rounds; r++) {
      const uint32_t start = micros();
      (void)walk(setSize);
      const uint32_t elapsed = micros() - start;
      if(elapsed < best) { best = elapsed; }
      yield();
    }
    return best;
  }
} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\r\n******** esp8266 icache bench"));
#ifdef MMU_ICACHE_SIZE
  Serial.printf_P(PSTR("MMU_ICACHE_SIZE=0x%X  MMU_IRAM_SIZE=0x%X\r\n"), MMU_ICACHE_SIZE, MMU_IRAM_SIZE);
#else
  Serial.println(F("MMU: core default (32K icache, 32K IRAM)"));
#endif
  constexpr uint8_t largeSetSize = static_cast<uint8_t>(sizeof(steps) / sizeof(steps[0]));
  Serial.printf_P(PSTR("CPU: %u MHz   workload spans %u bytes of flash\r\n"), ESP.getCpuFreqMHz(),
                  static_cast<uint32_t>(reinterpret_cast<uintptr_t>(steps[largeSetSize - 1U]) -
                                        reinterpret_cast<uintptr_t>(steps[0])));

  const uint32_t small = timeWalk(smallSetSize);
  const uint32_t large = timeWalk(largeSetSize);
  Serial.printf_P(PSTR("small set (%u fn): %u us total, %u ns per call\r\n"),
                  smallSetSize, small, (small * 1000UL) / (passes * 1UL * smallSetSize));
  Serial.printf_P(PSTR("large set (%u fn): %u us total, %u ns per call\r\n"),
                  largeSetSize, large, (large * 1000UL) / (passes * 1UL * largeSetSize));
  Serial.println(F("done"));
}

void loop() {
  delay(1000);
}
