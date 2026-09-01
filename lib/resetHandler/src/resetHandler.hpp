#pragma once

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Class for handling system resets.
class ResetHandler final {
public:
  /// @brief Resets the microcontroller unit (MCU) by triggering a watchdog reset.
  static void restartMCU();

#if defined(ESP8266) || defined(ESP32)
  /// @brief Retrieves the reason for the last system reset.
  /// @return A `uint8_t` value representing the reset reason, where each value corresponds to a specific reset cause.
  [[nodiscard]] static uint8_t getResetReason();

  /// @brief Returns true if the last reset was caused by any watchdog timer.
  [[nodiscard]] static bool isWdtReset();
#elif defined(__AVR_ATmega328P__)
  /// @brief Retrieves the reason for the last system reset.
  /// @details Captured during startup (see resetHandler.cpp): MCUSR itself always reads 0 by
  /// then, because urboot clears it after handing the flags over in r2. Layout:
  ///   - bits 0-3: MCUSR as the hardware left it (PORF, EXTRF, BORF, WDRF)
  ///   - bit 4:    `intentionalRestartFlag`, set when the watchdog reset came from restartMCU()
  ///   - bits 5-7: `RestartCause`, meaningful only while bit 4 is set
  ///
  /// urboot enters its serial wait on an external reset and restarts the part through its own
  /// watchdog, so EXTRF never reaches here: a reset pin and a hang both arrive as WDRF.
  /// @return The captured reset flags. Unlike the ESP builds this is a bitmask, not an enum.
  [[nodiscard]] static uint8_t getResetReason();

  /// @brief Bit set on top of MCUSR when the watchdog reset was asked for by restartMCU().
  /// @details MCUSR only uses bits 0-3 on this part, so bit 4 is free. Without it a deliberate
  /// restart and a hang the watchdog caught are both just WDRF.
  static constexpr uint8_t intentionalRestartFlag = 0x10U;

  /// @brief Why restartMCU() was called, carried in bits 5-7 of the reset reason.
  /// @details Every deliberate restart is WDRF plus `intentionalRestartFlag`, which alone cannot
  /// say which of them it was. Only meaningful while that flag is set.
  enum class RestartCause : uint8_t {
    Unspecified = 0U,       // restartMCU() called without one.
    InitFailed,             // A task refused to initialise at startup.
    CommandedOverCan,       // The gateway asked for it.
    OtaComplete             // Firmware was stored; rebooting into it.
  };

  /// @brief Position of RestartCause in the reset reason. Three bits, so eight causes fit.
  static constexpr uint8_t restartCauseShift = 5U;

  /// @brief Reads the cause back out of a reset reason.
  /// @param reason Value from getResetReason().
  /// @return The recorded cause; `Unspecified` when the restart was not a deliberate one.
  [[nodiscard]] static constexpr RestartCause getRestartCause(uint8_t reason) {
    return ((reason & intentionalRestartFlag) == 0U)
               ? RestartCause::Unspecified
               : static_cast<RestartCause>(reason >> restartCauseShift);
  }

  /// @brief Resets the MCU, recording why for the next startup to report.
  /// @param cause What prompted the restart.
  static void restartMCU(RestartCause cause);
#endif

  ResetHandler() = delete;                                           // Delete constructor.
  ~ResetHandler() = delete;                                          // Delete destructor.
  ResetHandler(const ResetHandler&) = delete;                       // Define copy constructor.
  ResetHandler& operator=(const ResetHandler&) = delete;            // Define copy assignment operator.
  ResetHandler(ResetHandler&&) = delete;                            // Define move constructor.
  ResetHandler& operator=(ResetHandler&&) = delete;                 // Define move assignment operator.
};
