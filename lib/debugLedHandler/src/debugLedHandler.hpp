#ifndef DEBUG_LED_HANDLER_HPP
#define DEBUG_LED_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <Arduino.h>                                                /// Arduino libraries header.
#if defined(ESP8266) || defined(ESP32)
#include <Ticker.h>                                                 /// Timer interrupt handler.
#endif

/// @brief A utility class for controlling a debug LED.
class DebugLedHandler final {
public:
  /// @brief Constructor to initialize the debug LED.
  /// @param debugLedPin The GPIO pin connected to the LED.
  /// @param ledOnState The logic level to turn the LED on (`1` for active-high, `0` for active-low).
  DebugLedHandler(uint8_t debugLedPin, uint8_t ledOnState = 1U);

  /// @brief Destructor for the debug LED handler.
  ~DebugLedHandler() = default;

  /// @brief Configures the LED pin and its active state.
  /// @param debugLedPin The GPIO pin connected to the LED.
  /// @param ledOnState The logic level to turn the LED on.
  static void setupLedPin(uint8_t debugLedPin, uint8_t ledOnState);

  /// @brief Turns the debug LED on.
  static void ledOn();

  /// @brief Turns the debug LED off.
  static void ledOff();

#if defined(__AVR_ATmega328P__)
  /// @brief Toggles the LED state on AVR platforms.
  static void ledToggle();
#elif defined(ESP8266) || defined(ESP32)
  /// @brief Toggles the LED state on ESP platforms.
  static IRAM_ATTR void ledToggle();

  /// @brief Starts blinking the LED at a fixed interval.
  /// @param tickIntervalMs The interval between LED toggles, in milliseconds.
  void startTicker(uint32_t tickIntervalMs);

  /// @brief Stops blinking the LED.
  void stopTicker();
#endif

  DebugLedHandler(const DebugLedHandler&) = delete;                       // Define copy constructor.
  DebugLedHandler& operator=(const DebugLedHandler&) = delete;            // Define copy assignment operator.
  DebugLedHandler(DebugLedHandler&&) = delete;                            // Define move constructor.
  DebugLedHandler& operator=(DebugLedHandler&&) = delete;                 // Define move assignment operator.

private:
  static constexpr uint8_t invalidPin = 0xFF;                             // Sentinel value indicating no pin is assigned.
  static uint8_t dbgLedPin;                                               // The GPIO pin connected to the LED.
  static uint8_t dbgLedOnState;                                           // The logic level to turn the LED on.
  static uint8_t ledState;                                                // Cached pin output level, avoids digitalRead() in ISR.
#if defined(ESP8266) || defined(ESP32)
  Ticker ledTicker;                                                       // Timer for managing periodic LED toggling.
#endif
};
#endif // DEBUG_LED_HANDLER_HPP