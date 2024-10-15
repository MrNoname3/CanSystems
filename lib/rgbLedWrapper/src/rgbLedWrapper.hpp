#ifndef RGB_LED_WRAPPER_HPP
#define RGB_LED_WRAPPER_HPP

#include <stdint.h>
#include <NeoPixelBus.h>                                            /// WS2812 LED driver library.

class RgbLedWrapper final {
public:
  RgbLedWrapper(uint8_t ledNumber, uint8_t ledPin);
  /// @brief Destructor of the object.
  ~RgbLedWrapper() = default;
  void begin();
  /// @brief Send the given data to the RGB LED(s).
  /// @param red Value of red color: 0-255.
  /// @param green Value of green color: 0-255.
  /// @param blue Value of blue color: 0-255.
  void setColor(uint8_t red, uint8_t green, uint8_t blue, bool saveColors);
  void loadColor();
  void clear();

  RgbLedWrapper(const RgbLedWrapper&) = delete;                       // Define copy constructor.
  RgbLedWrapper& operator=(const RgbLedWrapper&) = delete;            // Define copy assignment operator.
  RgbLedWrapper(RgbLedWrapper&&) = delete;                            // Define move constructor.
  RgbLedWrapper& operator=(RgbLedWrapper&&) = delete;                 // Define move assignment operator
private:
  NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> ledStrip;
  uint8_t redColor;
  uint8_t greenColor;
  uint8_t blueColor;
};
#endif // RGB_LED_WRAPPER_HPP