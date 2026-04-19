#include "common.hpp"
#include "BDDTest.h"
#include <string.h>

// ---- Time ----

bool test_time_hr_to_ms() {
  IT("hrToMs converts hours to milliseconds");
  IS_EQUAL(Time::hrToMs(0U),  0U);
  IS_EQUAL(Time::hrToMs(1U),  3600000U);
  IS_EQUAL(Time::hrToMs(24U), 86400000U);
  END_IT
}

bool test_time_min_to_ms() {
  IT("minToMs converts minutes to milliseconds");
  IS_EQUAL(Time::minToMs(0U),  0U);
  IS_EQUAL(Time::minToMs(1U),  60000U);
  IS_EQUAL(Time::minToMs(90U), 5400000U);
  END_IT
}

bool test_time_sec_to_ms() {
  IT("secToMs converts seconds to milliseconds");
  IS_EQUAL(Time::secToMs(0U),  0U);
  IS_EQUAL(Time::secToMs(1U),  1000U);
  IS_EQUAL(Time::secToMs(60U), 60000U);
  END_IT
}

bool test_time_hr_to_min() {
  IT("hrToMin converts hours to minutes");
  IS_EQUAL(Time::hrToMin(0U),  0U);
  IS_EQUAL(Time::hrToMin(1U),  60U);
  IS_EQUAL(Time::hrToMin(24U), 1440U);
  END_IT
}

bool test_time_ms_to_us() {
  IT("msToUs converts milliseconds to microseconds");
  IS_EQUAL(Time::msToUs(0U),    0U);
  IS_EQUAL(Time::msToUs(1U),    1000U);
  IS_EQUAL(Time::msToUs(1000U), 1000000U);
  END_IT
}

bool test_time_has_elapsed() {
  IT("hasElapsed returns true only when duration is strictly exceeded");
  IS_TRUE(Time::hasElapsed(200U, 100U, 50U));   // delta=100, 100 > 50 -> elapsed
  IS_FALSE(Time::hasElapsed(140U, 100U, 50U));  // delta=40,  40 > 50 -> not elapsed
  IS_FALSE(Time::hasElapsed(150U, 100U, 50U));  // delta=50, not strictly greater
  END_IT
}

bool test_time_has_elapsed_overflow() {
  IT("hasElapsed handles uint32_t timer overflow correctly");
  // Unsigned subtraction wraps: 0x05 - 0xFFFFFFF0 = 0x15 = 21
  IS_TRUE(Time::hasElapsed(0x00000005U, 0xFFFFFFF0U, 20U));   // 21 > 20 -> elapsed
  IS_FALSE(Time::hasElapsed(0x00000005U, 0xFFFFFFF0U, 25U));  // 21 > 25 -> not elapsed
  END_IT
}

// ---- Analog ----

bool test_analog_filter_full_new() {
  IT("complementaryFilter with alpha=255 returns the new value");
  IS_EQUAL(Analog::complementaryFilter<255U>(100U, 200U), 100U);
  END_IT
}

bool test_analog_filter_full_old() {
  IT("complementaryFilter with alpha=0 returns the old value");
  IS_EQUAL(Analog::complementaryFilter<0U>(100U, 200U), 200U);
  END_IT
}

bool test_analog_filter_mixed() {
  IT("complementaryFilter<128> blends new and old proportionally");
  // (128 * 100 + 127 * 200) / 255 = 38200 / 255 = 149
  IS_EQUAL(Analog::complementaryFilter<128U>(100U, 200U), 149U);
  END_IT
}

bool test_analog_filter10() {
  IT("complementaryFilter10 applies ~10% weight to new value");
  // alpha=25: (25 * 100 + 230 * 900) / 255 = 209500 / 255 = 821
  IS_EQUAL(Analog::complementaryFilter10(100U, 900U), 821U);
  END_IT
}

// ---- ErrorState ----

enum class TestErr : uint8_t { A = 0x01U, B = 0x02U, C = 0x04U };

bool test_error_state_initial_clear() {
  IT("ErrorState starts with no errors set");
  ErrorState<TestErr, uint8_t> state;
  IS_FALSE(state.hasAnyError());
  IS_EQUAL(state.getRawErrorState(), 0U);
  END_IT
}

bool test_error_state_set_and_check() {
  IT("setError and hasError track individual bits");
  ErrorState<TestErr, uint8_t> state;
  state.setError(TestErr::A);
  IS_TRUE(state.hasError(TestErr::A));
  IS_FALSE(state.hasError(TestErr::B));
  IS_TRUE(state.hasAnyError());
  END_IT
}

bool test_error_state_multiple_errors() {
  IT("multiple setError calls accumulate in the bitmask");
  ErrorState<TestErr, uint8_t> state;
  state.setError(TestErr::A);
  state.setError(TestErr::C);
  IS_EQUAL(state.getRawErrorState(), 0x05U);
  IS_TRUE(state.hasError(TestErr::A));
  IS_FALSE(state.hasError(TestErr::B));
  IS_TRUE(state.hasError(TestErr::C));
  END_IT
}

bool test_error_state_clear_single() {
  IT("clearError removes only the specified bit");
  ErrorState<TestErr, uint8_t> state;
  state.setError(TestErr::A);
  state.setError(TestErr::B);
  state.clearError(TestErr::A);
  IS_FALSE(state.hasError(TestErr::A));
  IS_TRUE(state.hasError(TestErr::B));
  END_IT
}

bool test_error_state_clear_all() {
  IT("clearAllErrors resets the entire bitmask");
  ErrorState<TestErr, uint8_t> state;
  state.setError(TestErr::A);
  state.setError(TestErr::B);
  state.clearAllErrors();
  IS_FALSE(state.hasAnyError());
  IS_EQUAL(state.getRawErrorState(), 0U);
  END_IT
}

// ---- Str ----

bool test_str_get_state_str() {
  IT("getStateStr returns [OK] for true and [ERR] for false");
  IS_TRUE(strcmp(Str::getStateStr(true),  "[OK]")  == 0);
  IS_TRUE(strcmp(Str::getStateStr(false), "[ERR]") == 0);
  END_IT
}

// ---- arraySize ----

bool test_array_size() {
  IT("arraySize returns the element count of a fixed-size array");
  uint8_t  arr3[3] = {};
  uint16_t arr5[5] = {};
  const uint32_t arr1[1] = {};
  IS_EQUAL(arraySize(arr3), 3U);
  IS_EQUAL(arraySize(arr5), 5U);
  IS_EQUAL(arraySize(arr1), 1U);
  END_IT
}

int main() {
  SUITE("Common");
  test_time_hr_to_ms();
  test_time_min_to_ms();
  test_time_sec_to_ms();
  test_time_hr_to_min();
  test_time_ms_to_us();
  test_time_has_elapsed();
  test_time_has_elapsed_overflow();
  test_analog_filter_full_new();
  test_analog_filter_full_old();
  test_analog_filter_mixed();
  test_analog_filter10();
  test_error_state_initial_clear();
  test_error_state_set_and_check();
  test_error_state_multiple_errors();
  test_error_state_clear_single();
  test_error_state_clear_all();
  test_str_get_state_str();
  test_array_size();
  FINISH
}
