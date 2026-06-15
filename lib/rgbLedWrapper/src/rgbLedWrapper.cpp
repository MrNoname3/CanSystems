#include "rgbLedWrapper.hpp"

RgbLedWrapper::RgbLedWrapper(const uint8_t ledNumber, const uint8_t ledPin) :
  ledStrip(ledNumber, ledPin),
  redColor(0U),
  greenColor(0U),
  blueColor(0U) {}

void RgbLedWrapper::begin() {
  ledStrip.Begin();               // Clear LEDs and show it.
  ledStrip.Show();
}

void RgbLedWrapper::setColor(const uint8_t red, const uint8_t green, const uint8_t blue, bool saveColors) {
  if(saveColors) {
    redColor = red;
    greenColor = green;
    blueColor = blue;
  }
  ledStrip.ClearTo(RgbColor(red, green, blue));
  ledStrip.Show();
}

void RgbLedWrapper::loadColor() {
  setColor(redColor, greenColor, blueColor, false);
}

void RgbLedWrapper::clear() {
  setColor(0U, 0U, 0U, true);
}
