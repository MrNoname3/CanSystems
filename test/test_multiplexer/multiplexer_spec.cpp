#include "multiplexer.hpp"
#include "Arduino.h"
#include "BDDTest.h"

static constexpr uint8_t kReadPin   = 10U;
static constexpr uint8_t kEnablePin = 11U;
static const uint8_t kSelPins[4]    = { 4U, 5U, 6U, 7U };

static Multiplexer make() {
  resetGpioState();
  return Multiplexer(kReadPin, kEnablePin, kSelPins);
}

// ---- constructor ----

bool test_constructor_sets_enable_pin_output_high() {
  IT("constructor configures enablePin as OUTPUT and sets it HIGH (disabled)");
  resetGpioState();
  Multiplexer mux(kReadPin, kEnablePin, kSelPins);
  IS_EQUAL(getPinMode(kEnablePin),          static_cast<uint8_t>(OUTPUT));
  IS_EQUAL(getDigitalWriteValue(kEnablePin), static_cast<uint8_t>(HIGH));
  END_IT
}

bool test_constructor_sets_select_pins_output() {
  IT("constructor configures all 4 select pins as OUTPUT");
  resetGpioState();
  Multiplexer mux(kReadPin, kEnablePin, kSelPins);
  for(uint8_t i = 0U; i < 4U; ++i) {
    IS_EQUAL(getPinMode(kSelPins[i]), static_cast<uint8_t>(OUTPUT));
  }
  END_IT
}

// ---- selectChannel ----

bool test_select_channel_0_all_pins_low() {
  IT("selectChannel(0) drives all select pins LOW");
  Multiplexer mux = make();
  mux.selectChannel(0U);
  for(uint8_t i = 0U; i < 4U; ++i) {
    IS_EQUAL(getDigitalWriteValue(kSelPins[i]), static_cast<uint8_t>(LOW));
  }
  END_IT
}

bool test_select_channel_1_bit0_high_rest_low() {
  IT("selectChannel(1) sets pin0 HIGH, pins 1-3 LOW");
  Multiplexer mux = make();
  mux.selectChannel(1U);
  IS_EQUAL(getDigitalWriteValue(kSelPins[0]), static_cast<uint8_t>(HIGH));
  IS_EQUAL(getDigitalWriteValue(kSelPins[1]), static_cast<uint8_t>(LOW));
  IS_EQUAL(getDigitalWriteValue(kSelPins[2]), static_cast<uint8_t>(LOW));
  IS_EQUAL(getDigitalWriteValue(kSelPins[3]), static_cast<uint8_t>(LOW));
  END_IT
}

bool test_select_channel_5_binary_0101() {
  IT("selectChannel(5) = binary 0101 → pins 0,2 HIGH; pins 1,3 LOW");
  Multiplexer mux = make();
  mux.selectChannel(5U); // 0b0101
  IS_EQUAL(getDigitalWriteValue(kSelPins[0]), static_cast<uint8_t>(HIGH));
  IS_EQUAL(getDigitalWriteValue(kSelPins[1]), static_cast<uint8_t>(LOW));
  IS_EQUAL(getDigitalWriteValue(kSelPins[2]), static_cast<uint8_t>(HIGH));
  IS_EQUAL(getDigitalWriteValue(kSelPins[3]), static_cast<uint8_t>(LOW));
  END_IT
}

bool test_select_channel_15_all_high() {
  IT("selectChannel(15) drives all select pins HIGH");
  Multiplexer mux = make();
  mux.selectChannel(15U); // 0b1111
  for(uint8_t i = 0U; i < 4U; ++i) {
    IS_EQUAL(getDigitalWriteValue(kSelPins[i]), static_cast<uint8_t>(HIGH));
  }
  END_IT
}

bool test_select_channel_wraps_lower_4_bits() {
  IT("selectChannel masks to 4 bits: channel 16 behaves like channel 0");
  Multiplexer mux = make();
  mux.selectChannel(16U); // 16 & 15 = 0
  for(uint8_t i = 0U; i < 4U; ++i) {
    IS_EQUAL(getDigitalWriteValue(kSelPins[i]), static_cast<uint8_t>(LOW));
  }
  END_IT
}

bool test_select_channel_wraps_0b10001_like_1() {
  IT("selectChannel(17) & 15 == 1: same pin pattern as selectChannel(1)");
  Multiplexer mux = make();
  mux.selectChannel(17U); // 17 & 15 = 1
  IS_EQUAL(getDigitalWriteValue(kSelPins[0]), static_cast<uint8_t>(HIGH));
  IS_EQUAL(getDigitalWriteValue(kSelPins[1]), static_cast<uint8_t>(LOW));
  END_IT
}

// ---- enable / disable ----

bool test_enable_read_sets_enable_pin_low() {
  IT("enableRead() drives enablePin LOW");
  Multiplexer mux = make();
  mux.enableRead();
  IS_EQUAL(getDigitalWriteValue(kEnablePin), static_cast<uint8_t>(LOW));
  END_IT
}

bool test_disable_read_sets_enable_pin_high() {
  IT("disableRead() drives enablePin HIGH");
  Multiplexer mux = make();
  mux.enableRead();
  mux.disableRead();
  IS_EQUAL(getDigitalWriteValue(kEnablePin), static_cast<uint8_t>(HIGH));
  END_IT
}

// ---- analogReadSimple / analogReadAdvanced ----

bool test_analog_read_simple_returns_adc_value() {
  IT("analogReadSimple returns the value from analogRead");
  Multiplexer mux = make();
  setAnalogReadValue(512U);
  IS_EQUAL(mux.analogReadSimple(3U), 512U);
  END_IT
}

bool test_analog_read_simple_enables_then_disables() {
  IT("analogReadSimple enables before read and disables after");
  Multiplexer mux = make();
  setAnalogReadValue(0U);
  static_cast<void>(mux.analogReadSimple(0U));
  IS_EQUAL(getDigitalWriteValue(kEnablePin), static_cast<uint8_t>(HIGH)); // disabled after read
  END_IT
}

bool test_analog_read_advanced_with_channel_returns_value() {
  IT("analogReadAdvanced(channel) selects channel and returns ADC value without toggling enable");
  Multiplexer mux = make();
  setAnalogReadValue(300U);
  IS_EQUAL(mux.analogReadAdvanced(7U), 300U);
  IS_EQUAL(getDigitalWriteValue(kEnablePin), static_cast<uint8_t>(HIGH)); // still disabled
  END_IT
}

bool test_analog_read_advanced_no_channel_returns_value() {
  IT("analogReadAdvanced() without channel argument returns ADC value");
  Multiplexer mux = make();
  setAnalogReadValue(1000U);
  IS_EQUAL(mux.analogReadAdvanced(), 1000U);
  END_IT
}

int main() {
  SUITE("Multiplexer");
  test_constructor_sets_enable_pin_output_high();
  test_constructor_sets_select_pins_output();
  test_select_channel_0_all_pins_low();
  test_select_channel_1_bit0_high_rest_low();
  test_select_channel_5_binary_0101();
  test_select_channel_15_all_high();
  test_select_channel_wraps_lower_4_bits();
  test_select_channel_wraps_0b10001_like_1();
  test_enable_read_sets_enable_pin_low();
  test_disable_read_sets_enable_pin_high();
  test_analog_read_simple_returns_adc_value();
  test_analog_read_simple_enables_then_disables();
  test_analog_read_advanced_with_channel_returns_value();
  test_analog_read_advanced_no_channel_returns_value();
  FINISH
}
