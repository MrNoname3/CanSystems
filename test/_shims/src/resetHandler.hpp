#pragma once
// Native-test shim for resetHandler (lib_ignored for native_test). restartMCU() must NOT actually
// terminate the test process, so it records the call instead; tests assert on restartCount.
#include <stdint.h>

class ResetHandler {
public:
  static void restartMCU() { ++restartCount; }

  static inline int restartCount = 0;
  static void resetState() { restartCount = 0; }

  ResetHandler() = delete;
};
