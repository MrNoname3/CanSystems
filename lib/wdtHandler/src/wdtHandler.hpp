#ifndef WDT_HANDLER_HPP
#define WDT_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <avr/wdt.h>                                                /// Watchdog timer library.

/// @brief Class for managing the Watchdog Timer (WDT).
class WdtHandler final {
public:
  /// @brief Enumeration for Watchdog Timer (WDT) timeout intervals.
  enum class WDT : uint8_t {
    T_15MS = WDTO_15MS,                                             // Timeout interval: 15 milliseconds.
    T_30MS = WDTO_30MS,                                             // Timeout interval: 30 milliseconds.
    T_60MS = WDTO_60MS,                                             // Timeout interval: 60 milliseconds.
    T_120MS = WDTO_120MS,                                           // Timeout interval: 120 milliseconds.
    T_250MS = WDTO_250MS,                                           // Timeout interval: 250 milliseconds.
    T_500MS = WDTO_500MS,                                           // Timeout interval: 500 milliseconds.
    T_1S = WDTO_1S,                                                 // Timeout interval: 1 second.
    T_2S = WDTO_2S,                                                 // Timeout interval: 2 seconds.
    T_4S = WDTO_4S,                                                 // Timeout interval: 4 seconds.
    T_8S = WDTO_8S                                                  // Timeout interval: 8 seconds.
  };

  /// @brief Constructs a `WdtHandler` object and enables the watchdog timer.
  /// @param wdtTime The timeout interval for the watchdog timer, specified as a
  /// value from the `WDT` enumeration.
  WdtHandler(WDT wdtTime) {
    wdt_enable(static_cast<uint8_t>(wdtTime));                      // Enable WDT timer.
  }

  /// @brief Destructor that disables the watchdog timer.
  /// Ensures the watchdog timer is disabled when the object goes out of scope.
  ~WdtHandler() {
    wdt_disable();                                                  // Disable WDT timer.
  }

  /// @brief Resets (feeds) the watchdog timer to prevent a system reset.
  /// This function must be called within the configured timeout interval to keep
  /// the system running. Failing to call this function in time will trigger a
  /// system reset.
  inline void feed() const {
    wdt_reset();                                                    // Reset the watchdog timer.
  }

  WdtHandler(const WdtHandler&) = delete;                       // Define copy constructor.
  WdtHandler& operator=(const WdtHandler&) = delete;            // Define copy assignment operator.
  WdtHandler(WdtHandler&&) = delete;                            // Define move constructor.
  WdtHandler& operator=(WdtHandler&&) = delete;                 // Define move assignment operator.
};
#endif // WDT_HANDLER_HPP