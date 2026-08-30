#pragma once
// Native-test shim for the vendored RCSwitch RF library. The real RCSwitch (now lib/RCSwitch,
// lib_ignore'd for native_test) is interrupt-driven and stores received frames in static state
// that a test cannot inject; this mirror exposes the same API and drives reception/transmission
// through static test hooks instead. The ESP build compiles rfHandler against the real class, so
// any signature divergence here surfaces as a native-only compile error.
#include <stdint.h>

class RCSwitch {
public:
  RCSwitch() = default;

  // ---- receiver ----
  void enableReceive(int32_t /*interrupt*/) {}                          // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] bool available() const { return rxAvailable; }     // NOLINT(readability-convert-member-functions-to-static)
  void resetAvailable() { rxAvailable = false; }                   // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] uint64_t getReceivedValue() const { return rxValue; }       // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] uint32_t getReceivedBitlength() const { return rxBitLength; }      // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] uint32_t getReceivedProtocol() const { return rxProtocol; }        // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] uint32_t getReceivedDelay() const { return rxDelay; }              // NOLINT(readability-convert-member-functions-to-static)

  // ---- transmitter ----
  void enableTransmit(int32_t /*pin*/) {}                              // NOLINT(readability-convert-member-functions-to-static)
  void setProtocol(int32_t protocol) { lastProtocol = protocol; }     // NOLINT(readability-convert-member-functions-to-static)
  void setPulseLength(int32_t pulseLength) { lastPulseLength = pulseLength; }  // NOLINT(readability-convert-member-functions-to-static)
  void send(uint64_t code, uint32_t length) {       // NOLINT(readability-convert-member-functions-to-static)
    lastSentCode = code;
    lastSentLength = length;
    ++sendCount;
  }

  // ---- test hooks (static so tests can drive them without a handle) ----
  // Queues a received frame and marks it available, as the ISR would on the real hardware.
  static void injectReceived(uint64_t value, uint32_t bitLength,
                             uint32_t protocol, uint32_t delay) {
    rxValue = value;
    rxBitLength = bitLength;
    rxProtocol = protocol;
    rxDelay = delay;
    rxAvailable = true;
  }
  static void resetState() {
    rxAvailable = false;
    rxValue = 0U;
    rxBitLength = 0U;
    rxProtocol = 0U;
    rxDelay = 0U;
    lastProtocol = 0;
    lastPulseLength = 0;
    lastSentCode = 0U;
    lastSentLength = 0U;
    sendCount = 0;
  }

  static inline bool rxAvailable = false;
  static inline uint64_t rxValue = 0U;
  static inline uint32_t rxBitLength = 0U;
  static inline uint32_t rxProtocol = 0U;
  static inline uint32_t rxDelay = 0U;
  static inline int32_t lastProtocol = 0;
  static inline int32_t lastPulseLength = 0;
  static inline uint64_t lastSentCode = 0U;
  static inline uint32_t lastSentLength = 0U;
  static inline int32_t sendCount = 0;
};
