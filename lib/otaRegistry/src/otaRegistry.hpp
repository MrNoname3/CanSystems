#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "sync.hpp"                                                 /// Mutex/lock guard (no-op off ESP32).

class OtaRegistry;                                                  // Forward declaration.

/// @brief What every target of one upload needs to know about the image, computed once.
/// @details The checksum only depends on the file, so the first transfer of a batch leaves it
/// here for the rest. `valid` is cleared when a new file arrives, which is the only moment the
/// contents can change.
struct OtaImageInfo {
  uint32_t size = 0U;                                               // Size of the image in bytes.
  uint16_t crc = 0U;                                                // CRC16 over the whole image.
  bool valid = false;                                               // Whether size and crc describe the current file.
};

/// @brief Abstract interface for objects that can receive OTA firmware updates triggered by file arrival.
class OtaTarget {
public:
  /// @brief Returns the firmware file name this target expects (PROGMEM pointer).
  [[nodiscard]] virtual const char* getFwFileName() const = 0;

  /// @brief Whether the target can be reached right now.
  /// @return `true` when a transfer to it has a chance of finishing.
  [[nodiscard]] virtual bool isOtaTargetOnline() const = 0;

  /// @brief Whether a transfer to this target is running.
  [[nodiscard]] virtual bool isOtaInProgress() const = 0;

  OtaTarget() = default;
  virtual ~OtaTarget() = default;
  OtaTarget(const OtaTarget&) = delete;
  OtaTarget& operator=(const OtaTarget&) = delete;
  OtaTarget(OtaTarget&&) = delete;
  OtaTarget& operator=(OtaTarget&&) = delete;

private:
  OtaTarget* next = nullptr;                                        // Intrusive linked list pointer, managed by OtaRegistry.
  bool otaQueued = false;                                           // Waiting for its turn in the current batch.
  friend class OtaRegistry;
};

/// @brief Hands one uploaded firmware to every matching target, one transfer at a time.
/// @details Targets share whatever carries the transfer, so running them together finishes no
/// sooner and keeps each of them in transfer for longer. The queue serialises them instead.
class OtaRegistry {
public:
  /// @brief Appends an OTA target to the registry. Called once per target at construction time.
  /// @param target Reference to the target to register.
  static void add(OtaTarget& target);

  /// @brief Queues every registered target expecting this file.
  /// @details Starts nothing itself: a target begins its own transfer from its own run(), so the
  /// task that received the file never reaches into the state another task is driving.
  /// @param fileName The validated file name (RAM string) to match against.
  static void queueForFile(const char* fileName);

  /// @brief Asks whether this target may begin its queued transfer now.
  /// @details Answers `true` at most once per queueing, and only while nothing else is
  /// transferring - the targets share one bus, so running them together finishes no sooner. A
  /// target that is not answering loses its turn rather than holding the queue up while it times
  /// out.
  /// @param target The caller, asking on its own behalf.
  /// @param image Receives the image facts known so far for this batch.
  /// @return `true` when the caller should start; `false` when it should not.
  [[nodiscard]] static bool claimStart(OtaTarget& target, OtaImageInfo& image);

  /// @brief Whether a transfer is still reading this file.
  /// @details True from the moment the targets are queued until the last of them has finished with
  /// it. A target holds the image open for the whole transfer, so replacing the file underneath it
  /// would leave it reading blocks the filesystem has already freed.
  /// @param fileName The file name to ask about (RAM string).
  /// @return `true` while the file must not be replaced.
  [[nodiscard]] static bool isFileInUse(const char* fileName);

  /// @brief Hands back what a target worked out about the image, for the rest of the batch.
  /// @param image Facts to keep; the next claim receives them.
  static void reportImage(const OtaImageInfo& image);

  OtaRegistry() = delete;

private:
  /// @brief Whether any registered target is mid-transfer.
  [[nodiscard]] static bool anyInProgress();

  static OtaTarget* head;                                           // Head of the intrusive linked list.
  static OtaImageInfo batchImage;                                   // Shared image facts for the queued batch.
  static RecursiveMutex mutex;                                      // Guards the queue flags and batchImage across tasks.
};
