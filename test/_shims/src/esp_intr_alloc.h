#pragma once
// Host stand-in for the ESP-IDF interrupt allocator. The handler the driver registers is kept
// so a test can invoke it directly, standing in for the peripheral raising its interrupt.

#include <stdint.h>

using intr_handle_t = void*;
using esp_err_t = int;

constexpr int ETS_CAN_INTR_SOURCE = 17;

/// @brief The interrupt handler the driver registered, and its argument.
struct Esp32IntrRegistration {
  void (*handler)(void*) = nullptr;
  void* arg = nullptr;
};
extern Esp32IntrRegistration esp32Intr;

inline esp_err_t esp_intr_alloc(int /*source*/, int /*flags*/, void (*handler)(void*), void* arg, intr_handle_t* handle) {
  esp32Intr.handler = handler;
  esp32Intr.arg = arg;
  if(handle != nullptr) { *handle = &esp32Intr; }
  return 0;
}

inline esp_err_t esp_intr_free(intr_handle_t /*handle*/) {
  esp32Intr.handler = nullptr;
  esp32Intr.arg = nullptr;
  return 0;
}

/// @brief Fires the registered handler, as the peripheral would.
inline void esp32TriggerCanInterrupt() {
  if(esp32Intr.handler != nullptr) { esp32Intr.handler(esp32Intr.arg); }
}
