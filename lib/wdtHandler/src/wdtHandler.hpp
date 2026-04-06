#ifndef WDT_HANDLER_HPP
#define WDT_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#if defined(__AVR_ATmega328P__)
#include <avr/wdt.h>                                                /// Watchdog timer library for AVR.
#elif defined(ESP8266)
#include <Esp.h>                                                    /// ESP8266 watchdog timer functions.
#elif defined(ESP32)
#include <esp_task_wdt.h>                                           /// ESP32 task watchdog timer functions.
#include <esp_err.h>                                                /// ESP32 error codes.
#include <freertos/task.h>                                          /// FreeRTOS task-related functions.
#endif

/// @brief A platform-agnostic utility class for managing Watchdog Timers (WDT).
class WdtHandler final {
public:
#if defined(__AVR_ATmega328P__)
  /// @brief Enumeration for Watchdog Timer (WDT) timeout intervals on AVR platforms.
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

  /// @brief Constructs a `WdtHandler` object and enables the watchdog timer with the specified timeout.
  /// @param wdtTime The timeout interval for the watchdog timer, specified as a value from the `WDT` enumeration.
  WdtHandler(WDT wdtTime) {
    enableWatchdog(wdtTime);
  }

  /// @brief Destructor that disables the watchdog timer.
  ~WdtHandler() { 
    disableWatchdog();
  }

  /// @brief Enables the watchdog timer on AVR platforms.
  /// @param wdtTime The timeout interval for the watchdog timer, specified as a value from the `WDT` enumeration.
  static inline void enableWatchdog(WDT wdtTime) {
    wdt_enable(static_cast<uint8_t>(wdtTime));
  }

  /// @brief Disables the watchdog timer on AVR platforms.
  static inline void disableWatchdog() {
    wdt_disable();
  }

  /// @brief Resets the watchdog timer to prevent a system reset.
  /// @note This function must be called periodically within the configured timeout interval.
  static inline void resetWatchdog() {
    wdt_reset();
  }
#elif defined(ESP8266)
  /// @brief Constructs a `WdtHandler` object and enables the watchdog timer.
  WdtHandler() {
    enableWatchdog();
  }

  /// @brief Destructor that disables the watchdog timer.
  ~WdtHandler() {
    disableWatchdog();
  }

  /// @brief Enables the watchdog timer on ESP8266.
  /// @details Disables the software watchdog and enables the hardware watchdog (~8400ms timeout).
  static inline void enableWatchdog() {
    wdt_disable();
  }

  /// @brief Disables the hardware watchdog timer on ESP8266.
  static inline void disableWatchdog() {
    wdt_enable(0U);
  }

  /// @brief Resets the watchdog timer to prevent a system reset.
  /// @note This function must be called periodically within the configured timeout interval.
  static inline void resetWatchdog() {
    wdt_reset();
  }
#elif defined(ESP32)
  /// @brief Deleted constructor to prevent instantiation of `WdtHandler` on ESP32.
  WdtHandler() = delete;

  /// @brief Deleted destructor to prevent instantiation of `WdtHandler` on ESP32.
  ~WdtHandler() = delete;

  /// @brief Enables the watchdog timer on ESP32.
  /// @param wdtTimeSec Timeout interval in seconds for the watchdog timer (default is `10s`).
  /// @param handle The task handle to be monitored by the watchdog (default is `nullptr`, which monitors the current task).
  /// @return `true` if the watchdog timer is successfully enabled, otherwise `false`.
  [[nodiscard]] static inline bool enableWatchdog(uint32_t wdtTimeSec = 10U, TaskHandle_t handle = nullptr) {
    const esp_err_t wdtInit = esp_task_wdt_init(wdtTimeSec, true);    // Enable panic too, so ESP32 restarts.
    const esp_err_t wdtAdded = esp_task_wdt_add(handle);
    return ((wdtInit == ESP_OK) && (wdtAdded == ESP_OK));
  }

  /// @brief Disables the watchdog timer on ESP32.
  /// @param handle The task handle to be removed from watchdog monitoring (default is `nullptr`, which removes the current task).
  /// @return `true` if the watchdog timer is successfully disabled, otherwise `false`.
  [[nodiscard]] static inline bool disableWatchdog(TaskHandle_t handle = nullptr) {
    const esp_err_t wdtDeleted = esp_task_wdt_delete(handle);
    const esp_err_t wdtDeinit = esp_task_wdt_deinit();
    return ((wdtDeinit == ESP_OK) && (wdtDeleted == ESP_OK));
  }

  /// @brief Resets the watchdog timer to prevent a system reset.
  /// @return `true` if the watchdog timer is successfully reset, otherwise `false`.
  [[nodiscard]] static inline bool resetWatchdog() {
    return (esp_task_wdt_reset() == ESP_OK);
  }
#endif

  WdtHandler(const WdtHandler&) = delete;                       // Define copy constructor.
  WdtHandler& operator=(const WdtHandler&) = delete;            // Define copy assignment operator.
  WdtHandler(WdtHandler&&) = delete;                            // Define move constructor.
  WdtHandler& operator=(WdtHandler&&) = delete;                 // Define move assignment operator.
};
#endif // WDT_HANDLER_HPP