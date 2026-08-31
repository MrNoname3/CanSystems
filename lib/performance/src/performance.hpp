#pragma once

#include <Arduino.h>                                                /// Arduino libraries header.
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "taskHandler.hpp"                                          /// Class for task scheduling.

/// @brief Tracks how long the scheduler takes to come back round to an ordinary task.
/// @details Measures the gap between two consecutive calls of its own `run()`, which under the
/// partial round-robin is one full turn of the rotation rather than one `loop()` iteration -
/// only the first task runs every pass. That is the number worth watching: it says how long a
/// plain task waits for its turn, so a task that blocks shows up here whichever one it is.
class Performance final : public Task {
public:
  /// @brief Constructs a `Performance` object with an initial round time limit and a callback.
  /// @param initialRoundTimeLimit Starting maximum in milliseconds; nothing below it is reported.
  /// @param maxRoundTimeCallback Called with the new maximum, in milliseconds, whenever one is
  /// recorded. May be `nullptr`.
  Performance(uint32_t initialRoundTimeLimit, void (*maxRoundTimeCallback)(uint32_t maxRoundTime)) :
    maxRoundTime(initialRoundTimeLimit),
    maxRoundTimeCallback(maxRoundTimeCallback) {}

  /// @brief Default destructor.
  ~Performance() override = default;

  /// @brief Initializes the performance tracker.
  /// @return Always returns `true`, indicating successful initialization.
  bool init() override {
    resetTimer();
    return true;
  }

  /// @brief Measures the time since this task's previous turn and reports a new maximum.
  /// @return `true`.
  bool run() override {
    const uint32_t actualTime = millis();
    const uint32_t actualRoundTime = actualTime - lastRunTime;
    lastRunTime = actualTime;
    if(actualRoundTime > maxRoundTime) {
      maxRoundTime = actualRoundTime;
      if(maxRoundTimeCallback != nullptr) {
        maxRoundTimeCallback(maxRoundTime);
      }
    }
    return true;
  }

  /// @brief Restarts the measurement from now, so the startup time is not reported as a round.
  inline void resetTimer() { lastRunTime = millis(); }

  Performance(const Performance&) = delete;                       // Define copy constructor.
  Performance& operator=(const Performance&) = delete;            // Define copy assignment operator.
  Performance(Performance&&) = delete;                            // Define move constructor.
  Performance& operator=(Performance&&) = delete;                 // Define move assignment operator.

private:
  uint32_t maxRoundTime;                                // Longest round recorded so far, in milliseconds.
  uint32_t lastRunTime = 0U;                            // millis() at this task's previous turn.
  void (*maxRoundTimeCallback)(uint32_t maxRoundTime);  // Notified when a new maximum is recorded.
};
