#pragma once
// One CAN controller and handler per test, wired to the register-level model in esp32CanModel.h.
// The handler is the real CanHandlerEsp32, so what a test observes here is what the driver
// actually left in the controller's transmit window - not a recorded call to send().
//
// A handler owns the device list its registered CanBase objects link themselves into, and that
// list has no remove operation, so a test must not share one with the next: each fixture builds
// its own and the frame helpers below read whichever is alive.
#include <stdint.h>
#include <string.h>
#include "ESP32SJA1000.h"
#include "canHandler.hpp"
#include "esp32CanModel.h"

class TestCan {
public:
  TestCan() { active = this; }
  ~TestCan() {
    if(active == this) { active = nullptr; }
  }

  TestCan(const TestCan&) = delete;
  TestCan& operator=(const TestCan&) = delete;
  TestCan(TestCan&&) = delete;
  TestCan& operator=(TestCan&&) = delete;

  // Lets a fixture stand in for the handler wherever one is expected.
  operator CanHandler&() { return handler; }  // NOLINT(google-explicit-constructor)

  ESP32SJA1000 controller;
  CanHandler handler{ controller };

  static inline TestCan* active = nullptr;
};

/// @brief Runs the handler until everything it has queued has reached the bus.
/// @details The controller takes one frame at a time, so a queued burst needs several passes;
/// the budget is well above the queue depth any suite here fills.
inline void pumpCanBus() {
  if(TestCan::active == nullptr) { return; }
  for(uint8_t i = 0U; i < 32U; i++) {
    (void)TestCan::active->handler.run();
  }
}

/// @brief The command field of a frame the model recorded.
[[nodiscard]] inline uint16_t canFrameCommand(const Esp32CanModel::SentFrame& sent) {
  CanHandler::CanFrame decoded;
  decoded.extId = sent.id;
  return static_cast<uint16_t>(decoded.cmd);
}

/// @brief How many frames carrying `cmd` have reached the bus.
[[nodiscard]] inline size_t countCanFrames(uint16_t cmd) {
  pumpCanBus();
  size_t count = 0U;
  for(const Esp32CanModel::SentFrame& sent : esp32Can.transmitted()) {
    // cppcheck-suppress useStlAlgorithm
    if(canFrameCommand(sent) == cmd) { ++count; }
  }
  return count;
}

/// @brief The last frame carrying `cmd`, or `nullptr` when none reached the bus.
[[nodiscard]] inline const CanHandler::CanFrame* lastCanFrame(uint16_t cmd) {
  pumpCanBus();
  static CanHandler::CanFrame decoded;   // Outlives the call so the callers keep their pointer shape.
  bool found = false;
  for(const Esp32CanModel::SentFrame& sent : esp32Can.transmitted()) {
    if(canFrameCommand(sent) != cmd) { continue; }
    decoded.extId = sent.id;
    memcpy(decoded.data, sent.data, sizeof(decoded.data));
    found = true;
  }
  return found ? &decoded : nullptr;
}
