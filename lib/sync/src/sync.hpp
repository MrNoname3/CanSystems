#pragma once
// Locking for state a FreeRTOS task may end up sharing with another. On ESP32 the mutex is real
// whether or not a second task exists yet, so a guarded member is safe the moment one is added.
// Everywhere else there is a single thread of execution and these compile away.

#if defined(ESP32)
#include "freertos/FreeRTOS.h"                                      /// FreeRTOS base.
#include "freertos/semphr.h"                                        /// FreeRTOS semaphores/mutexes.

/// @brief Recursive mutex backed by a FreeRTOS recursive mutex (ESP32).
/// @details Recursive by design: the MQTT receive callback runs inside PubSubClient::loop() and
/// may publish a response on the same task, which a plain mutex would deadlock on.
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

  /// @brief Whether the underlying semaphore exists.
  /// @details lock() and unlock() do nothing without one, so a guard would report success while
  /// leaving the data unprotected.
  /// @return `true` when the mutex can actually lock.
  [[nodiscard]] bool valid() const { return handle != nullptr; }

  RecursiveMutex(const RecursiveMutex&) = delete;                   // Define copy constructor.
  RecursiveMutex& operator=(const RecursiveMutex&) = delete;        // Define copy assignment operator.
  RecursiveMutex(RecursiveMutex&&) = delete;                        // Define move constructor.
  RecursiveMutex& operator=(RecursiveMutex&&) = delete;             // Define move assignment operator.

private:
  SemaphoreHandle_t handle = xSemaphoreCreateRecursiveMutex();      // FreeRTOS recursive mutex handle.
};

#else  // Single-threaded platforms: no-op primitives that compile away.

/// @brief No-op recursive mutex for single-threaded platforms (ESP8266 NONOS / AVR / native).
/// @details The methods are empty and inline, so the optimizer removes them entirely. A shared
/// library such as `connectivity` can therefore lock for the ESP32 task model without the other
/// platforms paying anything for it.
class RecursiveMutex final {
public:
  RecursiveMutex() = default;

  /// @brief Takes the lock. Nothing to take here, so this compiles away.
  void lock() {}

  /// @brief Releases one level of the lock. Nothing to release here, so this compiles away.
  void unlock() {}

  /// @brief There is nothing to create here, so the lock is always usable.
  /// @return Always `true`.
  [[nodiscard]] bool valid() const { return true; }  // NOLINT(readability-convert-member-functions-to-static) mirrors the ESP32 signature

  RecursiveMutex(const RecursiveMutex&) = delete;                   // Define copy constructor.
  RecursiveMutex& operator=(const RecursiveMutex&) = delete;        // Define copy assignment operator.
  RecursiveMutex(RecursiveMutex&&) = delete;                        // Define move constructor.
  RecursiveMutex& operator=(RecursiveMutex&&) = delete;             // Define move assignment operator.
};

#endif

/// @brief RAII lock guard: locks on construction, unlocks on destruction. No-op when the mutex is.
class LockGuard final {
public:
  explicit LockGuard(RecursiveMutex& mutex) :
    mutex(mutex) { mutex.lock(); }
  ~LockGuard() { mutex.unlock(); }

  LockGuard(const LockGuard&) = delete;                             // Define copy constructor.
  LockGuard& operator=(const LockGuard&) = delete;                  // Define copy assignment operator.
  LockGuard(LockGuard&&) = delete;                                  // Define move constructor.
  LockGuard& operator=(LockGuard&&) = delete;                       // Define move assignment operator.

private:
  RecursiveMutex& mutex;                                            // The guarded mutex.
};
