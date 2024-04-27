#ifndef PUSHBUTTON_HANDLER_HPP
#define PUSHBUTTON_HANDLER_HPP

#include <stdint.h>
#include "PushButtonClicks.hpp"
#include "canHandler/src/canHandler.hpp"
#include <HardwareSerial.h>

class PushButtonHandler final {
public:
  enum class BtnEvent : uint8_t {
    NONE = 0,
    LONG_PRESS,
    LONG_RELEASE,
    ONE_PRESS,
    TWO_PRESS,
    THREE_PRESS,
    FOUR_PRESS,
    FIVE_PRESS,
    LAST_ELEMENT
  };
  PushButtonHandler(HardwareSerial& serial, const CanHandler& canHandler, uint8_t buttonPin);
  /// @brief Destructor of the object.
  ~PushButtonHandler() = default;
  void loop();
  void addBtnCallback(void (*btnCallback)(BtnEvent btnEvent));
  PushButtonHandler(const PushButtonHandler&) = delete;               // Define copy constructor.
  PushButtonHandler& operator=(const PushButtonHandler&) = delete;    // Define copy assignment operator.
  PushButtonHandler(PushButtonHandler&&) = delete;                    // Define move constructor.
  PushButtonHandler& operator=(PushButtonHandler&&) = delete;         // Define move assignment operator.
private:
  HardwareSerial& serialPort;
  const CanHandler& canHandler;
  const uint8_t buttonPin;
  static constexpr uint8_t deadTime = 250U;
  static constexpr uint16_t longPressTime = 500U;
  static constexpr uint8_t debounceTime = 70U;
  static constexpr bool buttonPolarity = false;
  PushButton button;
  void (*btnCallback)(BtnEvent btnEvent);
};
#endif // PUSHBUTTON_HANDLER_HPP