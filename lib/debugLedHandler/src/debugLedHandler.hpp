#ifndef DEBUG_LED_HANDLER_HPP
#define DEBUG_LED_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <Arduino.h>                                                /// Arduino libraries header.
#if defined(ESP8266) || defined(ESP32)
#include <Ticker.h>                                                 /// Timer interrupt hadnler.
#endif

/// @brief A platform-agnostic utility class for controlling a debug LED.
/// @tparam LedOnState Specifies the logic level that turns the LED on.
/// Use `1` if the LED is active-high, or `0` if the LED is active-low.
template<uint8_t LedOnState>
class DebugLedHandler final {
public:
  /// @brief Constructor for the debug LED handler.
  /// @param debugLedPin The pin connected to the debug LED.
  DebugLedHandler(uint8_t debugLedPin) {
    addLedPin(debugLedPin);
  }

  /// @brief Destructor for the debug LED handler.
  ~DebugLedHandler() = default;

  /// @brief Associates a new LED pin for debugging.
  /// @param debugLedPin The pin connected to the debug LED.
  static inline void addLedPin(uint8_t debugLedPin) {
    ledPin = debugLedPin;
    pinMode(ledPin, OUTPUT);
    ledOff();
  }

  /// @brief Turns the debug LED on.
  static inline void ledOn() {
    if(ledPin == invalidPin) {return;}
    digitalWrite(ledPin, (static_cast<bool>(LedOnState) ? HIGH : LOW));
  }

  /// @brief Turns the debug LED off.
  static inline void ledOff() {
    if(ledPin == invalidPin) {return;}
    digitalWrite(ledPin, (static_cast<bool>(LedOnState) ? LOW : HIGH));
  }

#if defined(__AVR_ATmega328P__)
  /// @brief Toggles the current state of the debug LED.
  static inline void ledToggle() {
    if(ledPin == invalidPin) {return;}
    digitalWrite(ledPin, !digitalRead(ledPin));
  }
#elif defined(ESP8266) || defined(ESP32)
  /// @brief Toggles the current state of the debug LED.
  static inline IRAM_ATTR void ledToggle() {
    if(ledPin == invalidPin) {return;}
    digitalWrite(ledPin, !digitalRead(ledPin));
  }

  /// @brief Starts a periodic blinking mechanism using a hardware timer.
  /// @param tickIntervalMs The interval, in milliseconds, between LED toggles.
  void startTicker(uint32_t tickIntervalMs) {
    ledOff();
    ledTicker.attach_ms(tickIntervalMs, ledToggle);
  }

  /// @brief Stops the periodic blinking mechanism.
  void stopTicker() {
    ledTicker.detach();
    ledOff();
  }
#endif

  DebugLedHandler(const DebugLedHandler&) = delete;                       // Define copy constructor.
  DebugLedHandler& operator=(const DebugLedHandler&) = delete;            // Define copy assignment operator.
  DebugLedHandler(DebugLedHandler&&) = delete;                            // Define move constructor.
  DebugLedHandler& operator=(DebugLedHandler&&) = delete;                 // Define move assignment operator.

private:
  static constexpr uint8_t invalidPin = 0xFF;                             // Sentinel value indicating no pin is assigned.
  static uint8_t ledPin;                                                  // The pin connected to the debug LED (ESP).
#if defined(ESP8266) || defined(ESP32)
  Ticker ledTicker;                                                       // Timer for managing periodic LED toggling.
#endif
};
template<uint8_t LedOnState>
uint8_t DebugLedHandler<LedOnState>::ledPin = DebugLedHandler<LedOnState>::invalidPin;
#endif // DEBUG_LED_HANDLER_HPP