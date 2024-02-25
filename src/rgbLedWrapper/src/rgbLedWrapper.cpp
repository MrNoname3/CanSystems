#include "rgbLedWrapper.hpp"

RgbLedWrapper::RgbLedWrapper(const uint8_t ledNumber, const uint8_t ledPin) :
  ledStrip(ledNumber, ledPin)
{}

void RgbLedWrapper::begin() {
  ledStrip.Begin();               // Clear LEDs and show it.
  ledStrip.Show();
}

void RgbLedWrapper::setColor(const uint8_t red, const uint8_t green, const uint8_t blue) {
  ledStrip.ClearTo(RgbColor(red, green, blue));
  ledStrip.Show();
}

void RgbLedWrapper::clear() {
  setColor( 0U, 0U, 0U);
}