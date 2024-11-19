#ifndef RGB_LED_WRAPPER_HPP
#define RGB_LED_WRAPPER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <NeoPixelBus.h>                                            /// WS2812 LED driver library.

/// @brief Wrapper class for controlling an RGB LED strip using the NeoPixelBus library.
/// @details This class provides simplified methods to control WS2812-based LED strips, including setting colors,
/// saving/loading previous colors, and clearing the LEDs.
class RgbLedWrapper final {
public:
  /// @brief Constructs an `RgbLedWrapper` object.
  /// @param ledNumber The number of LEDs in the strip.
  /// @param ledPin The GPIO pin connected to the LED strip's data line.
  RgbLedWrapper(uint8_t ledNumber, uint8_t ledPin);

  /// @brief Destructor of the object.
  ~RgbLedWrapper() = default;

  /// @brief Initializes the LED strip.
  /// @details Prepares the LED strip for operation and clears it.
  void begin();

  /// @brief Sets the color of the RGB LED(s).
  /// @param red Value of the red component (0-255).
  /// @param green Value of the green component (0-255).
  /// @param blue Value of the blue component (0-255).
  /// @param saveColors If true, saves the color values for future restoration via `loadColor()`.
  void setColor(uint8_t red, uint8_t green, uint8_t blue, bool saveColors);

  /// @brief Restores and sets the last saved color on the LED(s).
  void loadColor();

  /// @brief Clears the LED(s) by turning them off.
  /// @details Also saves the "off" state as the current color.
  void clear();

  RgbLedWrapper(const RgbLedWrapper&) = delete;                       // Define copy constructor.
  RgbLedWrapper& operator=(const RgbLedWrapper&) = delete;            // Define copy assignment operator.
  RgbLedWrapper(RgbLedWrapper&&) = delete;                            // Define move constructor.
  RgbLedWrapper& operator=(RgbLedWrapper&&) = delete;                 // Define move assignment operator
private:
  NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> ledStrip;      // NeoPixelBus object to manage the LED strip.
  uint8_t redColor;                                           // Saved red component value (0-255).
  uint8_t greenColor;                                         // Saved green component value (0-255).
  uint8_t blueColor;                                          // Saved blue component value (0-255).
};
#endif // RGB_LED_WRAPPER_HPP