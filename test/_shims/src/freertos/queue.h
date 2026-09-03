#pragma once
// Native-test shim for the FreeRTOS queues the CAN handler moves frames through. Items are
// copied in and out as raw bytes, exactly as the real queue does, and the depth is enforced:
// a full queue refuses the send, which is the branch the handler counts as a dropped frame.
// Nothing blocks - a host test drives time itself, so a timeout would only stall the suite.
#include "projdefs.h"
#include <string.h>
#include <deque>
#include <vector>

struct ShimQueue {
  size_t itemSize = 0U;
  size_t capacity = 0U;
  std::deque<std::vector<uint8_t>> items;
};

using QueueHandle_t = ShimQueue*;

inline QueueHandle_t xQueueCreate(size_t length, size_t itemSize) {
  // Owned for the process lifetime, as the real queues are: the handler never deletes them, and
  // a deque keeps the references handed out earlier valid as more are created.
  static std::deque<ShimQueue> pool;
  pool.emplace_back();
  pool.back().itemSize = itemSize;
  pool.back().capacity = length;
  return &pool.back();
}

inline BaseType_t xQueueSend(QueueHandle_t handle, const void* item, TickType_t /*ticks*/) {
  if((handle == nullptr) || (handle->items.size() >= handle->capacity)) { return pdFALSE; }
  const uint8_t* bytes = static_cast<const uint8_t*>(item);
  handle->items.emplace_back(bytes, bytes + handle->itemSize);
  return pdTRUE;
}

inline BaseType_t xQueueSendFromISR(QueueHandle_t handle, const void* item, BaseType_t* woken) {
  if(woken != nullptr) { *woken = pdFALSE; }
  return xQueueSend(handle, item, 0U);
}

inline BaseType_t xQueueReceive(QueueHandle_t handle, void* buffer, TickType_t /*ticks*/) {
  if((handle == nullptr) || handle->items.empty()) { return pdFALSE; }
  memcpy(buffer, handle->items.front().data(), handle->itemSize);
  handle->items.pop_front();
  return pdTRUE;
}

// ---- test helpers (no FreeRTOS counterpart) ----
inline size_t shimQueueDepth(QueueHandle_t handle) { return (handle == nullptr) ? 0U : handle->items.size(); }
