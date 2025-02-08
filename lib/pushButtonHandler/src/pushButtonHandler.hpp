#ifndef PUSHBUTTON_HANDLER_HPP
#define PUSHBUTTON_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "PushButtonClicks.hpp"                                     /// Push button handling class.
#include "canHandler.hpp"                                           /// CAN handler library.
#include "taskHandler.hpp"                                          /// Class for task scheduling.

/// @brief Handles button press events and communicates them over CAN or via callbacks.
/// @details The `PushButtonHandler` class processes button press events (e.g., long presses, single taps, multiple taps)
/// using the `PushButton` class and sends the events via CAN communication or invokes a callback.
class PushButtonHandler final : public Task {
public:
  /// @brief Enum representing different button press events.
  enum class BtnEvent : uint8_t {
    NONE = 0,                 // No button press event detected.
    LONG_PRESS,               // Long press event detected.
    LONG_RELEASE,             // Long release event detected.
    ONE_PRESS,                // Single press detected.
    TWO_PRESS,                // Double press detected.
    THREE_PRESS,              // Triple press detected.
    FOUR_PRESS,               // Quadruple press detected.
    FIVE_PRESS,               // Quintuple press detected.
    LAST_ELEMENT              // Sentinel value for event enumeration.
  };

  /// @brief Constructs a `PushButtonHandler` object.
  /// @param canHandler Reference to the CAN handler object for sending button events.
  /// @param buttonReader Function pointer to a button state reader function (e.g., a `digitalRead()` wrapper).
  PushButtonHandler(const CanHandler& canHandler, bool (*buttonReader)());

  /// @brief Destructor of the object.
  ~PushButtonHandler() = default;

  /// @brief Initializes the handler.
  /// @details This function is intentionally left empty in this implementation but can be overridden if needed.
  /// @return `true`.
  virtual bool init() override { return true; };

  /// @brief Processes button events and sends them via CAN or callback.
  /// @details Checks for button press events, sends them to the CAN handler, and invokes a user-defined callback if set.
  /// @return `true`.
  virtual bool run() override;

  /// @brief Adds a callback function for button press events.
  /// @param btnCallback Function pointer to the callback to handle button events.
  /// @note The callback function should accept a `BtnEvent` enum value as its parameter.
  void addBtnCallback(void (*btnCallback)(BtnEvent btnEvent));

  PushButtonHandler(const PushButtonHandler&) = delete;               // Define copy constructor.
  PushButtonHandler& operator=(const PushButtonHandler&) = delete;    // Define copy assignment operator.
  PushButtonHandler(PushButtonHandler&&) = delete;                    // Define move constructor.
  PushButtonHandler& operator=(PushButtonHandler&&) = delete;         // Define move assignment operator.

private:
  static constexpr uint8_t deadTime = 250U;             // Pause duration (in ms) after the last button event.
  static constexpr uint16_t longPressTime = 500U;       // Duration (in ms) to classify a press as a long press.
  static constexpr uint8_t debounceTime = 70U;          // Debounce duration (in ms) to filter out noisy signals.
  static constexpr bool buttonPolarity = false;         // Polarity of the button when pressed (`true` for HIGH, `false` for LOW).

  const CanHandler& canHandler;                         // Reference to the CAN handler object.
  bool (*readButtonValue)();                            // Function pointer for reading the button state.
  PushButton button;                                    // Instance of the `PushButton` class to handle button press events.
  void (*btnCallback)(BtnEvent btnEvent);               // Function pointer for the button event callback (if set).
};
#endif // PUSHBUTTON_HANDLER_HPP