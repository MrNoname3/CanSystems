#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.

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

  /// @brief Triggers the OTA update for this target.
  /// @param image Shared image facts for this batch: read when `valid`, filled in when not.
  virtual void triggerOta(OtaImageInfo& image) = 0;

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

  /// @brief Queues every registered target expecting this file, and starts the first of them.
  /// @param fileName The validated file name (RAM string) to match against.
  static void queueForFile(const char* fileName);

  /// @brief Starts the next queued target, if nothing is transferring.
  /// @details Called by a target that has just finished, and by queueForFile().
  static void startNext();

  OtaRegistry() = delete;

private:
  /// @brief Whether any registered target is mid-transfer.
  [[nodiscard]] static bool anyInProgress();

  static OtaTarget* head;                                           // Head of the intrusive linked list.
  static OtaImageInfo batchImage;                                   // Shared image facts for the queued batch.
};
