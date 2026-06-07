#include "moistureReader.hpp"
#include "Arduino.h"
#include "BDDTest.h"
#include <string.h>

// Hardware pins for the multiplexer used across the tests.
static constexpr uint8_t kReadPin   = 10U;
static constexpr uint8_t kEnablePin = 11U;
static const uint8_t     kSelPins[4] = { 4U, 5U, 6U, 7U };
static const uint8_t     kChannels[2] = { 3U, 5U };

// Module timing constants (mirrored from moistureReader.hpp for readable test arithmetic).
static constexpr uint32_t kWakeupMs    = 10000U;  // sensorWakeupTime
static constexpr uint32_t kFilteringMs = 2000U;   // filteringTime
static constexpr uint32_t kReadTime    = 1000U;   // interval between cycles

// ---- dataSender capture ----
static uint8_t  g_sendData[8][8];
static uint32_t g_sendCount;
static void onData(const uint8_t (&d)[8]) {
  if(g_sendCount < 8U) { memcpy(g_sendData[g_sendCount], d, 8U); }
  ++g_sendCount;
}
static void resetCapture() {
  g_sendCount = 0U;
  memset(g_sendData, 0, sizeof(g_sendData));
}

// complementaryFilter10(V, 0) after a single READING step: floor(25 * V / 255).
static uint16_t singleStepFilter(uint16_t v) {
  return static_cast<uint16_t>((25U * static_cast<uint32_t>(v)) / 255U);
}

// ---- basics ----

bool test_init_and_run_return_true() {
  IT("init() and run() return true");
  resetGpioState();
  resetCapture();
  Multiplexer mux(kReadPin, kEnablePin, kSelPins);
  RgbLedWrapper led(1U, 7U);
  MoistureReader<2> mr(mux, led, kChannels, kReadTime, onData);
  setFakeMillis(0U);
  IS_TRUE(mr.init());
  IS_TRUE(mr.run());
  clearFakeMillis();
  END_IT
}

bool test_no_send_before_read_time() {
  IT("no measurement starts (no send) before readTime elapses");
  resetGpioState();
  resetCapture();
  Multiplexer mux(kReadPin, kEnablePin, kSelPins);
  RgbLedWrapper led(1U, 7U);
  MoistureReader<2> mr(mux, led, kChannels, kReadTime, onData);
  setFakeMillis(0U);
  (void)mr.init();              // eventTimer = 0, state IDLE
  setFakeMillis(kReadTime / 2U);
  (void)mr.run();               // readTime not elapsed -> still IDLE
  IS_EQUAL(g_sendCount, 0U);
  IS_EQUAL(getDigitalWriteValue(kEnablePin), static_cast<uint8_t>(HIGH));  // mux still disabled
  clearFakeMillis();
  END_IT
}

// ---- full cycle ----

bool test_full_cycle_reads_all_channels_in_order() {
  IT("a full cycle reads both channels in order and packs channel + 16-bit value");
  resetGpioState();
  resetCapture();
  Multiplexer mux(kReadPin, kEnablePin, kSelPins);
  RgbLedWrapper led(1U, 7U);
  MoistureReader<2> mr(mux, led, kChannels, kReadTime, onData);

  setAnalogReadValue(520U);             // single-step filter -> floor(25*520/255) = 50
  const uint16_t expected = singleStepFilter(520U);

  setFakeMillis(0U);
  (void)mr.init();                      // eventTimer = 0, IDLE

  uint32_t t = kReadTime + 1U;          // readTime elapsed
  setFakeMillis(t);
  (void)mr.run();                       // IDLE -> WAKEUP (enableRead drives enable LOW)
  IS_EQUAL(getDigitalWriteValue(kEnablePin), static_cast<uint8_t>(LOW));

  t += kWakeupMs + 1U;
  setFakeMillis(t);
  (void)mr.run();                       // WAKEUP -> SETUP

  // Channel 0: SETUP then a single READING step that also crosses filteringTime -> one send.
  setFakeMillis(t);
  (void)mr.run();                       // SETUP -> READING (selectChannel(3), eventTimer = t, value = 0)
  t += kFilteringMs + 1U;
  setFakeMillis(t);
  (void)mr.run();                       // READING: value = filter(520,0); filteringTime elapsed -> send ch0

  // Channel 1: same pattern.
  setFakeMillis(t);
  (void)mr.run();                       // SETUP -> READING (selectChannel(5))
  t += kFilteringMs + 1U;
  setFakeMillis(t);
  (void)mr.run();                       // READING -> send ch1, cycle ends -> IDLE, mux disabled

  IS_EQUAL(g_sendCount, 2U);
  IS_EQUAL(g_sendData[0][0], kChannels[0]);
  IS_EQUAL(g_sendData[0][1], static_cast<uint8_t>(expected & 0xFFU));         // low byte
  IS_EQUAL(g_sendData[0][2], static_cast<uint8_t>((expected >> 8U) & 0xFFU)); // high byte
  IS_EQUAL(g_sendData[1][0], kChannels[1]);
  IS_EQUAL(g_sendData[1][1], static_cast<uint8_t>(expected & 0xFFU));
  IS_EQUAL(getDigitalWriteValue(kEnablePin), static_cast<uint8_t>(HIGH));     // disabled after cycle
  clearFakeMillis();
  END_IT
}

bool test_select_channel_pins_match_last_channel() {
  IT("the select pins reflect the channel being read (channel 5 = 0b0101)");
  resetGpioState();
  resetCapture();
  Multiplexer mux(kReadPin, kEnablePin, kSelPins);
  RgbLedWrapper led(1U, 7U);
  MoistureReader<2> mr(mux, led, kChannels, kReadTime, onData);

  setAnalogReadValue(100U);
  setFakeMillis(0U);
  (void)mr.init();
  uint32_t t = kReadTime + 1U;
  setFakeMillis(t); (void)mr.run();                 // -> WAKEUP
  t += kWakeupMs + 1U; setFakeMillis(t); (void)mr.run(); // -> SETUP
  setFakeMillis(t); (void)mr.run();                 // SETUP -> READING channel 0 (3 = 0b0011)
  IS_EQUAL(getDigitalWriteValue(kSelPins[0]), static_cast<uint8_t>(HIGH));
  IS_EQUAL(getDigitalWriteValue(kSelPins[1]), static_cast<uint8_t>(HIGH));
  IS_EQUAL(getDigitalWriteValue(kSelPins[2]), static_cast<uint8_t>(LOW));
  IS_EQUAL(getDigitalWriteValue(kSelPins[3]), static_cast<uint8_t>(LOW));
  clearFakeMillis();
  END_IT
}

// ---- triggerImmediateMeasurement ----

bool test_trigger_immediate_starts_cycle_early() {
  IT("triggerImmediateMeasurement() starts a cycle before readTime would elapse");
  resetGpioState();
  resetCapture();
  Multiplexer mux(kReadPin, kEnablePin, kSelPins);
  RgbLedWrapper led(1U, 7U);
  const uint32_t longReadTime = 100000U;
  MoistureReader<2> mr(mux, led, kChannels, longReadTime, onData);

  setFakeMillis(0U);
  (void)mr.init();                      // eventTimer = 0
  setFakeMillis(50000U);
  (void)mr.run();                       // longReadTime not elapsed -> still IDLE
  IS_EQUAL(getDigitalWriteValue(kEnablePin), static_cast<uint8_t>(HIGH));

  setFakeMillis(200000U);
  mr.triggerImmediateMeasurement();     // eventTimer = 200000 - 100000 = 100000
  setFakeMillis(200001U);
  (void)mr.run();                       // now elapsed -> IDLE -> WAKEUP, enableRead
  IS_EQUAL(getDigitalWriteValue(kEnablePin), static_cast<uint8_t>(LOW));
  clearFakeMillis();
  END_IT
}

bool test_trigger_immediate_ignored_when_not_idle() {
  IT("triggerImmediateMeasurement() is ignored while a cycle is already running");
  resetGpioState();
  resetCapture();
  Multiplexer mux(kReadPin, kEnablePin, kSelPins);
  RgbLedWrapper led(1U, 7U);
  MoistureReader<2> mr(mux, led, kChannels, kReadTime, onData);

  setFakeMillis(0U);
  (void)mr.init();
  setFakeMillis(kReadTime + 1U);
  (void)mr.run();                       // IDLE -> WAKEUP (now running)
  // Trigger during WAKEUP must not restart/disturb: enable stays LOW, no send yet.
  mr.triggerImmediateMeasurement();
  IS_EQUAL(getDigitalWriteValue(kEnablePin), static_cast<uint8_t>(LOW));
  IS_EQUAL(g_sendCount, 0U);
  clearFakeMillis();
  END_IT
}

int main() {
  SUITE("MoistureReader");
  test_init_and_run_return_true();
  test_no_send_before_read_time();
  test_full_cycle_reads_all_channels_in_order();
  test_select_channel_pins_match_last_channel();
  test_trigger_immediate_starts_cycle_early();
  test_trigger_immediate_ignored_when_not_idle();
  FINISH
}
