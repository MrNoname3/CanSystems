#pragma once
//
// Minimal cross-platform synchronization primitives.
//
// On ESP32 (FreeRTOS, multi-task) `RecursiveMutex` wraps a FreeRTOS recursive mutex. On every other
// platform (ESP8266 NONOS, AVR, native) the code runs single-threaded and cooperatively, so the
// primitives compile to nothing: empty inline methods that the optimizer removes entirely — no flash,
// no CPU, and (aside from the empty member itself) no RAM cost. This lets shared libraries such as
// `connectivity` add locking for the ESP32 task model without affecting the ESP8266 build.
//
// Recursive (re-entrant) by design: the MQTT RX callback runs inside PubSubClient::loop() and may
// publish a response on the same thread, which would otherwise self-deadlock a plain mutex.
//

#if defined(ESP32)
#include "freertos/FreeRTOS.h"                                      /// FreeRTOS base.
#include "freertos/semphr.h"                                        /// FreeRTOS semaphores/mutexes.

/// @brief Recursive mutex backed by a FreeRTOS recursive mutex (ESP32).
class RecursiveMutex final {
public:
  RecursiveMutex() = default;

  ~RecursiveMutex() {
    if(handle != nullptr) { vSemaphoreDelete(handle); }
  }

  /// @brief Acquires the mutex, blocking until available (recursive: same task may re-enter).
  void lock() {  // NOLINT(readability-convert-member-functions-to-static) instance owns the mutex handle
    if(handle != nullptr) { (void)xSemaphoreTakeRecursive(handle, portMAX_DELAY); }
  }

  /// @brief Releases one level of the recursive lock.
  void unlock() {  // NOLINT(readability-convert-member-functions-to-static) instance owns the mutex handle
    if(handle != nullptr) { (void)xSemaphoreGiveRecursive(handle); }
  }

  RecursiveMutex(const RecursiveMutex&) = delete;                   // Define copy constructor.
  RecursiveMutex& operator=(const RecursiveMutex&) = delete;        // Define copy assignment operator.
  RecursiveMutex(RecursiveMutex&&) = delete;                        // Define move constructor.
  RecursiveMutex& operator=(RecursiveMutex&&) = delete;             // Define move assignment operator.

private:
  SemaphoreHandle_t handle = xSemaphoreCreateRecursiveMutex();      // FreeRTOS recursive mutex handle.
};

#else  // Single-threaded platforms: no-op primitives that compile away.

/// @brief No-op recursive mutex for single-threaded platforms (ESP8266 NONOS / AVR / native).
class RecursiveMutex final {
public:
  RecursiveMutex() = default;
  void lock() {}     // Compiles to nothing.
  void unlock() {}   // Compiles to nothing.

  RecursiveMutex(const RecursiveMutex&) = delete;                   // Define copy constructor.
  RecursiveMutex& operator=(const RecursiveMutex&) = delete;        // Define copy assignment operator.
  RecursiveMutex(RecursiveMutex&&) = delete;                        // Define move constructor.
  RecursiveMutex& operator=(RecursiveMutex&&) = delete;             // Define move assignment operator.
};

#endif

/// @brief RAII lock guard: locks on construction, unlocks on destruction. No-op when the mutex is.
class LockGuard final {
public:
  explicit LockGuard(RecursiveMutex& mutex) : mutex(mutex) { mutex.lock(); }
  ~LockGuard() { mutex.unlock(); }

  LockGuard(const LockGuard&) = delete;                             // Define copy constructor.
  LockGuard& operator=(const LockGuard&) = delete;                  // Define copy assignment operator.
  LockGuard(LockGuard&&) = delete;                                  // Define move constructor.
  LockGuard& operator=(LockGuard&&) = delete;                       // Define move assignment operator.

private:
  RecursiveMutex& mutex;                                            // The guarded mutex.
};
