#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.

class OtaRegistry;                                                  // Forward declaration.

/// @brief Abstract interface for objects that can receive OTA firmware updates triggered by file arrival.
class OtaTarget {
public:
  /// @brief Returns the firmware file name this target expects (PROGMEM pointer).
  [[nodiscard]] virtual const char* getFwFileName() const = 0;

  /// @brief Triggers the OTA update for this target.
  virtual void triggerOta() = 0;

  OtaTarget() = default;
  virtual ~OtaTarget() = default;
  OtaTarget(const OtaTarget&) = delete;
  OtaTarget& operator=(const OtaTarget&) = delete;
  OtaTarget(OtaTarget&&) = delete;
  OtaTarget& operator=(OtaTarget&&) = delete;

private:
  OtaTarget* next = nullptr;                                        // Intrusive linked list pointer, managed by OtaRegistry.
  friend class OtaRegistry;
};

/// @brief Static registry for OTA-capable targets using an intrusive linked list.
class OtaRegistry {
public:
  /// @brief Appends an OTA target to the registry. Called once per target at construction time.
  /// @param target Reference to the target to register.
  static void add(OtaTarget& target);

  /// @brief Triggers OTA on all registered targets whose firmware file name matches.
  /// @param fileName The validated file name (RAM string) to match against.
  static void triggerForFile(const char* fileName);

  OtaRegistry() = delete;

private:
  static OtaTarget* head;                                           // Head of the intrusive linked list.
};
