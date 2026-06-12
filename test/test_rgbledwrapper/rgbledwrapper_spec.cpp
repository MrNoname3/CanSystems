#include "rgbLedWrapper.hpp"
#include "BDDTest.h"

// The NeoPixelBus shim records Begin/Show/ClearTo calls statically.
using LedStrip = NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod>;

bool test_begin_initialises_and_shows() {
  IT("begin() initialises the strip and pushes the cleared state");
  LedStrip::resetState();
  RgbLedWrapper led(4U, 7U);
  led.begin();
  IS_EQUAL(LedStrip::beginCount, 1);
  IS_EQUAL(LedStrip::showCount, 1);
  END_IT
}

bool test_set_color_drives_the_strip() {
  IT("setColor() pushes the requested color to the strip");
  LedStrip::resetState();
  RgbLedWrapper led(4U, 7U);
  led.setColor(10U, 20U, 30U, false);
  IS_EQUAL(LedStrip::clearToCount, 1);
  IS_EQUAL(LedStrip::lastColor.R, 10U);
  IS_EQUAL(LedStrip::lastColor.G, 20U);
  IS_EQUAL(LedStrip::lastColor.B, 30U);
  END_IT
}

bool test_load_color_restores_saved_color() {
  IT("loadColor() restores the last saved color, ignoring unsaved overrides");
  LedStrip::resetState();
  RgbLedWrapper led(4U, 7U);
  led.setColor(10U, 20U, 30U, true);      // saved
  led.setColor(1U, 2U, 3U, false);        // temporary override (playback-style)
  led.loadColor();
  IS_EQUAL(LedStrip::lastColor.R, 10U);
  IS_EQUAL(LedStrip::lastColor.G, 20U);
  IS_EQUAL(LedStrip::lastColor.B, 30U);
  END_IT
}

bool test_clear_turns_off_and_saves_off_state() {
  IT("clear() turns the strip off and saves the off state for loadColor()");
  LedStrip::resetState();
  RgbLedWrapper led(4U, 7U);
  led.setColor(10U, 20U, 30U, true);
  led.clear();
  IS_EQUAL(LedStrip::lastColor.R, 0U);
  led.loadColor();                        // saved state is now "off"
  IS_EQUAL(LedStrip::lastColor.R, 0U);
  IS_EQUAL(LedStrip::lastColor.G, 0U);
  IS_EQUAL(LedStrip::lastColor.B, 0U);
  END_IT
}

int main() {
  SUITE("RgbLedWrapper");
  test_begin_initialises_and_shows();
  test_set_color_drives_the_strip();
  test_load_color_restores_saved_color();
  test_clear_turns_off_and_saves_off_state();
  FINISH
}
