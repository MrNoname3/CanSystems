#pragma once
// Native-test shim for the FreeRTOS mutex the CAN handler guards its device list with. The host
// suite is single-threaded, so the mutex only has to be takeable and releasable; what it must
// model faithfully is that a failed creation yields a null handle, which the handler checks.
#include "projdefs.h"
#include <deque>

struct ShimSemaphore {
  bool taken = false;
};

using SemaphoreHandle_t = ShimSemaphore*;

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  // Owned for the process lifetime, as the real mutex is: nothing ever deletes it.
  static std::deque<ShimSemaphore> pool;
  pool.emplace_back();
  return &pool.back();
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t /*ticks*/) {
  if(handle == nullptr) { return pdFALSE; }
  handle->taken = true;
  return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
  if(handle == nullptr) { return pdFALSE; }
  handle->taken = false;
  return pdTRUE;
}
