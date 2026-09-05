#pragma once
// Native-test shim for the FreeRTOS mutexes the CAN handler guards its device list with. The host
// suite is single-threaded, so a take that finds the mutex already held is the re-entrant case,
// which is exactly where a plain mutex and a recursive one part company: the plain one refuses.
// What it must also model faithfully is that a failed creation yields a null handle, which the
// handler checks.
#include "projdefs.h"
#include <deque>

struct ShimSemaphore {
  uint32_t heldCount = 0U;
  bool recursive = false;
};

using SemaphoreHandle_t = ShimSemaphore*;

namespace ShimSemaphorePool {
  /// @brief Hands out a semaphore owned for the process lifetime, as the real ones are.
  inline SemaphoreHandle_t create(bool recursive) {
    static std::deque<ShimSemaphore> pool;
    pool.emplace_back();
    pool.back().recursive = recursive;
    return &pool.back();
  }
} // namespace ShimSemaphorePool

inline SemaphoreHandle_t xSemaphoreCreateMutex() { return ShimSemaphorePool::create(false); }

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return ShimSemaphorePool::create(true); }

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t /*ticks*/) {
  if(handle == nullptr) { return pdFALSE; }
  // Held already, and nothing else can release it on a single thread: the wait can only time out.
  if(handle->heldCount != 0U) { return pdFALSE; }
  handle->heldCount = 1U;
  return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
  if(handle == nullptr) { return pdFALSE; }
  handle->heldCount = 0U;
  return pdTRUE;
}

inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t handle, TickType_t /*ticks*/) {
  if(handle == nullptr) { return pdFALSE; }
  handle->heldCount++;
  return pdTRUE;
}

inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t handle) {
  if(handle == nullptr) { return pdFALSE; }
  if(handle->heldCount != 0U) { handle->heldCount--; }
  return pdTRUE;
}
