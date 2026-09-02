#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief How far the startup sequence has got.
/// @details Each step records itself on entry, so a device that restarts can report where the
/// previous run stopped instead of leaving a bare watchdog reset to be guessed at. The numbers are
/// stored and published: keep them stable and only ever append.
enum class BootStage : uint8_t {
  Unknown = 0U,        // Nothing on record: the first boot, or the previous run lost power.
  Banner = 1U,         // Startup began; no step has been reached yet.
  FileSystem = 2U,     // Mounting the file system.
  BackoffWait = 3U,    // Sitting out the reconnect backoff.
  NetworkStart = 4U,   // Bringing the network interface up.
  AddressWait = 5U,    // Waiting for the router to hand out an address.
  ClockSync = 6U,      // Synchronising the clock over NTP.
  Credentials = 7U,    // Reading the broker credentials and building the topics.
  Certificate = 8U,    // Loading the CA certificate.
  BrokerConnect = 9U,  // TCP, TLS and the MQTT CONNECT.
  Subscribe = 10U,     // Subscribing to the command topic.
  Announce = 11U,      // Publishing availability, discovery and device info.
  Running = 12U,       // Startup finished; the main loop has the device.
};

/// @brief Records how far startup got, in memory that outlives a reset but not a power cycle.
/// @details That lifetime is the point: a reset - watchdog, restartMCU() or otherwise - keeps the
/// record, so the next run can say which step the device died on, while pulling the plug is a
/// deliberate clean slate. Where the record lives is platform business and stays in the .cpp.
class BootProgress final {
public:
  /// @brief Reads what the previous run reached, then marks this one as started.
  /// @note Call once, as early in startup as the logger allows; every later `set()` builds on it.
  static void begin();

  /// @brief Records that startup has reached `stage`.
  static void set(BootStage stage);

  /// @brief How far the previous run got, or `Unknown` when nothing was on record.
  /// @return The stage read by `begin()`; `Unknown` before `begin()` has run.
  [[nodiscard]] static BootStage getPrevious();

  /// @brief The stage this run has reached.
  [[nodiscard]] static BootStage getCurrent();

  /// @brief Human-readable name of a stage, for the startup log.
  [[nodiscard]] static const char* getName(BootStage stage);

  BootProgress() = delete;                                          // Delete constructor.
  ~BootProgress() = delete;                                         // Delete destructor.
  BootProgress(const BootProgress&) = delete;                       // Delete copy constructor.
  BootProgress& operator=(const BootProgress&) = delete;            // Delete copy assignment operator.
  BootProgress(BootProgress&&) = delete;                            // Delete move constructor.
  BootProgress& operator=(BootProgress&&) = delete;                 // Delete move assignment operator.

private:
  static BootStage previousStage;                                   // What the previous run reached.
  static BootStage currentStage;                                    // What this run has reached.
};
