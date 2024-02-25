#ifndef RGB_LED_WRAPPER_HPP
#define RGB_LED_WRAPPER_HPP

#include <stdint.h>
#include <NeoPixelBus.h>                      /// WS2812 LED driver library.

class RgbLedWrapper final {
public:
  RgbLedWrapper(const uint8_t ledNumber, const uint8_t ledPin);
   /// @brief Destructor of the object.
  ~RgbLedWrapper() = default;
  void begin();
  /// @brief Send the given data to the RGB LED(s).
  /// @param red Value of red color: 0-255.
  /// @param green Value of green color: 0-255.
  /// @param blue Value of blue color: 0-255.
  void setColor(const uint8_t red, const uint8_t green, const uint8_t blue);
  void clear();

  RgbLedWrapper(const RgbLedWrapper&) = delete;                       // Define copy constructor.
  RgbLedWrapper& operator=(const RgbLedWrapper&) = delete;            // Define copy assignment operator.
  RgbLedWrapper(RgbLedWrapper&&) = delete;                            // Define move constructor.
  RgbLedWrapper& operator=(RgbLedWrapper&&) = delete;                 // Define move assignment operator
private:
  NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> ledStrip;
};
#endif // RGB_LED_WRAPPER_HPP