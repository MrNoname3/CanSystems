#pragma once
// Native-test shim for the ESP Ticker: stores the attached callback so tests can fire the
// periodic tick manually instead of waiting for real time.
#include <stdint.h>

class Ticker {
public:
  using callback_t = void (*)();

  void attach_ms(uint32_t ms, callback_t cb) {
    intervalMs = ms;
    callback = cb;
    lastAttached = this;
  }

  void detach() {
    callback = nullptr;
    if(lastAttached == this) { lastAttached = nullptr; }
  }

  // ---- test helpers ----
  // Fires the most recently attached ticker (the owner usually keeps it private).
  static void fireLast() {
    if((lastAttached != nullptr) && (lastAttached->callback != nullptr)) { lastAttached->callback(); }
  }
  static void resetState() { lastAttached = nullptr; }

private:
  callback_t callback = nullptr;
  uint32_t intervalMs = 0U;
  static inline Ticker* lastAttached = nullptr;
};
