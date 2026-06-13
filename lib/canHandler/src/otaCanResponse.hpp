#ifndef OTA_CAN_RESPONSE_HPP
#define OTA_CAN_RESPONSE_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "ota.hpp"                                                  /// OTA::OtaState.

/// @brief Device-side decision logic for the OTA-over-CAN handshake: given how the OTA storage
/// state machine just advanced, decide which CAN response the device must send back to the gateway
/// and whether to reboot into the freshly stored firmware.
/// @details Kept free of the AVR-only CAN / Logger / reset plumbing so it can be unit-tested on the
/// host. The canHandlerAtmega328P run() loop that consumes this decision is #ifdef ARDUINO_ARCH_AVR
/// and never runs in native_test, so extracting the decision is what makes it testable.
namespace OtaCanResponse {
  /// @brief The CAN response a state transition requires.
  enum class Action : uint8_t {
    NONE,        // No response this pass (transfer still in progress or idle).
    ACK_START,   // Storage accepted the start -> ACK the OTA_START.
    ACK_END,     // Firmware validated -> ACK the OTA_END.
    NACK_END     // Firmware invalid -> NACK the OTA_END.
  };

  /// @brief What the device should do after one OTA storage step.
  struct Decision {
    Action action = Action::NONE;   // CAN response to emit.
    bool reboot = false;            // Reboot into the new firmware (only when it targets this device).
  };

  /// @brief Decides the response for an OTA storage state transition.
  /// @param prev The OTA storage state before this run() step.
  /// @param curr The OTA storage state after this run() step.
  /// @param isOwnFw True if the stored firmware targets this device (flash block 0).
  /// @return The CAN response to send and whether to reboot.
  /// @details The original handler used three independent ifs; because they are mutually exclusive
  /// on `curr`, this if/else-if chain is behaviour-identical. reboot is set only on a valid update
  /// that targets this device, so storing another node's firmware never reboots this one.
  [[nodiscard]] inline Decision decide(OTA::OtaState prev, OTA::OtaState curr, bool isOwnFw) {
    Decision decision;
    if((prev == OTA::OtaState::START) && (curr == OTA::OtaState::STORE)) {
      decision.action = Action::ACK_START;
    } else if(curr == OTA::OtaState::VALID) {
      decision.action = Action::ACK_END;
      decision.reboot = isOwnFw;
    } else if(curr == OTA::OtaState::INVALID) {
      decision.action = Action::NACK_END;
    }
    return decision;
  }
} // namespace OtaCanResponse
#endif // OTA_CAN_RESPONSE_HPP
