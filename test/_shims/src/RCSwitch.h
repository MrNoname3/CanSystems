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
  void enableReceive(int /*interrupt*/) {}                          // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] bool available() const { return rxAvailable; }     // NOLINT(readability-convert-member-functions-to-static)
  void resetAvailable() { rxAvailable = false; }                   // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] unsigned long long getReceivedValue() const { return rxValue; }       // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] unsigned int getReceivedBitlength() const { return rxBitLength; }      // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] unsigned int getReceivedProtocol() const { return rxProtocol; }        // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] unsigned int getReceivedDelay() const { return rxDelay; }              // NOLINT(readability-convert-member-functions-to-static)

  // ---- transmitter ----
  void enableTransmit(int /*pin*/) {}                              // NOLINT(readability-convert-member-functions-to-static)
  void setProtocol(int protocol) { lastProtocol = protocol; }     // NOLINT(readability-convert-member-functions-to-static)
  void setPulseLength(int pulseLength) { lastPulseLength = pulseLength; }  // NOLINT(readability-convert-member-functions-to-static)
  void send(unsigned long long code, unsigned int length) {       // NOLINT(readability-convert-member-functions-to-static)
    lastSentCode = code;
    lastSentLength = length;
    ++sendCount;
  }

  // ---- test hooks (static so tests can drive them without a handle) ----
  // Queues a received frame and marks it available, as the ISR would on the real hardware.
  static void injectReceived(unsigned long long value, unsigned int bitLength,
                             unsigned int protocol, unsigned int delay) {
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
  static inline unsigned long long rxValue = 0U;
  static inline unsigned int rxBitLength = 0U;
  static inline unsigned int rxProtocol = 0U;
  static inline unsigned int rxDelay = 0U;
  static inline int lastProtocol = 0;
  static inline int lastPulseLength = 0;
  static inline unsigned long long lastSentCode = 0U;
  static inline unsigned int lastSentLength = 0U;
  static inline int sendCount = 0;
};
