#pragma once
#include <Arduino.h>                                                /// PROGMEM, strlcpy.
#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Bookkeeping behind the retained [DIAG] message: why the device went offline, for how
/// long, and how often it has come back.
/// @details Kept free of the MQTT client and the network stack so it can be unit-tested on the
/// host - `connectivity` itself is lib_ignored for native_test, which is why this lives in
/// `common` rather than beside its only caller. The caller formats the wall-clock drop time and
/// hands it in; everything else - which cause wins, the offline duration, the reconnect count,
/// and reporting an outage exactly once - is decided here.
class DisconnectDiag final {
public:
  static constexpr uint8_t dropTimeBufSize = 24U;   // ISO8601 UTC stamp + terminator; matches Connectivity.

  /// @brief Everything the diagnostics payload is built from.
  struct Report {
    const char* cause = nullptr;              // What took the device offline.
    const char* dropTime = nullptr;           // UTC ISO8601 stamp of the drop; empty when the clock was unset.
    uint32_t offlineSeconds = 0U;             // How long the outage lasted.
    uint32_t reconnectCount = 0U;             // Successful reconnects since boot, this one included.
  };

  /// @brief Records the outage that has just started.
  /// @param networkUp Whether the local network link is still up.
  /// @param mqttCause Cause reported by the MQTT client.
  /// @param isoDropTime UTC ISO8601 stamp of the drop; `nullptr` when the clock was unset.
  /// @param atMs `millis()` at the moment the drop was detected.
  /// @note A dropped link outranks `mqttCause`: MQTT only reports a failure because the link
  /// went away underneath it, so the link is the root cause.
  void recordDisconnect(bool networkUp, const char* mqttCause, const char* isoDropTime, uint32_t atMs) {
    dropCause = networkUp ? mqttCause : networkLostCause;
    dropDetectedMs = atMs;
    if(isoDropTime != nullptr) {
      strlcpy(dropTime, isoDropTime, sizeof(dropTime));
    } else {
      dropTime[0] = '\0';
    }
  }

  /// @brief Takes the report for an outage that has just ended, if there is one.
  /// @param atMs `millis()` at the moment the device came back.
  /// @param report Receives the report when this returns `true`.
  /// @return `false` when no outage is on record - the first connect after boot, or an outage
  /// already reported. The reconnect count only moves when this returns `true`.
  [[nodiscard]] bool takeReport(uint32_t atMs, Report& report) {
    if(dropCause == nullptr) { return false; }
    reconnectCount++;
    report.cause = dropCause;
    report.dropTime = dropTime;
    // Unsigned subtraction: an outage spanning the millis() wrap still gives its real length.
    report.offlineSeconds = (atMs - dropDetectedMs) / 1000U;
    report.reconnectCount = reconnectCount;
    dropCause = nullptr;
    return true;
  }

  /// @brief The cause recorded when the network link itself dropped.
  [[nodiscard]] static const char* getNetworkLostCause() { return networkLostCause; }

private:
  static constexpr const char PROGMEM networkLostCause[] = "NETWORK_LOST";

  const char* dropCause = nullptr;                  // Non-null while an outage is waiting to be reported.
  char dropTime[dropTimeBufSize] = { '\0' };        // UTC ISO8601 stamp of the drop.
  uint32_t dropDetectedMs = 0U;                     // millis() when the drop was detected.
  uint32_t reconnectCount = 0U;                     // Successful reconnects since boot.
};
