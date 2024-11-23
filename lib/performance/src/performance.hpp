#ifndef PERFORMANCE_HPP
#define PERFORMANCE_HPP

#include <Arduino.h>                                                /// Arduino libraries header.
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "taskHandler.hpp"                                          /// Class for task scheduling.

/// @brief Performance measurement class to track the execution time of each loop iteration.
class Performance final : public Task {
public:
  /// @brief Constructs a `Performance` object with an initial loop time limit and a callback.
  /// @param initialLoopTimeLimit The initial maximum loop time limit in milliseconds.
  /// This value sets a baseline for comparison when tracking loop execution times.
  /// @param maxLoopTimeCallback A callback function to be called when a new maximum loop 
  /// time is recorded. The function should accept a single `uint32_t` parameter, which 
  /// represents the new maximum loop time in milliseconds.
  Performance(uint32_t initialLoopTimeLimit, void (*maxLoopTimeCallback)(uint32_t maxLoopTime)) :
    maxLoopTime(initialLoopTimeLimit),
    lastLoopTime(0U),
    maxLoopTimeCallback(maxLoopTimeCallback)
  {}

  /// @brief Default destructor.
  ~Performance() = default;

  /// @brief Initializes the performance measurement.
  /// This function records the initial time (using `millis()`) to calculate loop times.
  /// @return Always returns `true` to indicate successful initialization.
  virtual bool init() override {
    lastLoopTime = millis();
    return true;
  }

  /// @brief Runs the performance measurement and checks the loop execution time.
  /// This function calculates the time taken since the last loop iteration and checks if 
  /// it exceeds the current maximum loop time. If a new maximum is recorded, it calls the 
  /// provided callback function (if not null).
  virtual void run() override {
    const uint32_t actualTime = millis();
    uint32_t actualLoopTime = actualTime - lastLoopTime;
    lastLoopTime = actualTime;
    if(actualLoopTime > maxLoopTime) {
      maxLoopTime = actualLoopTime;
      if(maxLoopTimeCallback != nullptr) {
        maxLoopTimeCallback(maxLoopTime);
      }
    }
  }

  Performance(const Performance&) = delete;                       // Define copy constructor.
  Performance& operator=(const Performance&) = delete;            // Define copy assignment operator.
  Performance(Performance&&) = delete;                            // Define move constructor.
  Performance& operator=(Performance&&) = delete;                 // Define move assignment operator.

private:
  uint32_t maxLoopTime;                                           // The maximum loop time recorded in milliseconds.
  uint32_t lastLoopTime;                                          // The timestamp of the last loop iteration.
  void (*maxLoopTimeCallback)(uint32_t maxLoopTime);              // The callback function for notifying maximum loop time.
};
#endif // PERFORMANCE_HPP