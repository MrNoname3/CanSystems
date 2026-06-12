#include "dfPlayer.hpp"
#include "Arduino.h"
#include "Stream.h"
#include "BDDTest.h"

// The Stream shim captures every byte DFPlayerMiniFast writes; packets are 10 bytes:
// [0]=0x7E [1]=0xFF [2]=len [3]=command [4]=feedback [5]=paramMSB [6]=paramLSB [7..8]=crc [9]=0xEF.
static constexpr uint8_t CMD_PLAY   = 0x03U;
static constexpr uint8_t CMD_VOLUME = 0x06U;
static constexpr uint8_t CMD_STOP   = 0x16U;

static constexpr uint8_t RX_PIN = 5U;
static constexpr uint8_t TX_PIN = 6U;
static constexpr uint8_t EN_PIN = 9U;
static constexpr uint8_t BUSY_PIN = 3U;

using LedStrip = NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod>;

static void resetEnv() {
  resetGpioState();
  LedStrip::resetState();
  Stream::clearCaptured();
  setFakeMillis(0U);
}

static size_t packetCount() { return Stream::captured.size() / 10U; }
static uint8_t packetCmd(size_t index) { return Stream::captured[index * 10U + 3U]; }
static uint8_t packetMsb(size_t index) { return Stream::captured[index * 10U + 5U]; }
static uint8_t packetLsb(size_t index) { return Stream::captured[index * 10U + 6U]; }

// Advances the fake clock and runs the task once.
static void step(Task& task, uint32_t& now, uint32_t advanceMs) {
  now += advanceMs;
  setFakeMillis(now);
  (void)task.run();
}

// Walks a freshly queued track up to the point where the PLAY command was just sent
// (state: WAIT_FOR_PLAY). Returns the current fake time.
static uint32_t walkToPlaying(Task& task) {
  uint32_t now = 0U;
  setFakeMillis(now);
  step(task, now, 0U);        // IDLE -> TURN_ON
  step(task, now, 0U);        // TURN_ON: power pins HIGH
  step(task, now, 1001U);     // WAIT_FOR_BOOT elapsed -> SET_VOLUME
  step(task, now, 0U);        // SET_VOLUME: volume packet (+ LED color)
  step(task, now, 121U);      // WAIT_FOR_CMD elapsed -> PLAY
  step(task, now, 0U);        // PLAY: play packet, busy interrupt armed
  return now;
}

// Finishes the track: busy interrupt fires, queue empties, module powers off.
static void finishTrack(Task& task, uint32_t now) {
  triggerInterrupt(BUSY_PIN);
  step(task, now, 0U);        // WAIT_FOR_PLAY -> CHECK_QUEUE
  step(task, now, 0U);        // CHECK_QUEUE (empty) -> TURN_OFF
  step(task, now, 0U);        // TURN_OFF -> IDLE
}

// ---- tests ----

bool test_idle_without_queue_does_nothing() {
  IT("an empty queue keeps the module powered off and sends nothing");
  resetEnv();
  RgbLedWrapper rgbLed(19U, 7U);
  DFPlayer player(rgbLed, RX_PIN, TX_PIN, EN_PIN, BUSY_PIN);
  Task& task = player;
  uint32_t now = 0U;
  step(task, now, 0U);
  step(task, now, 5000U);
  IS_EQUAL(packetCount(), 0U);
  IS_EQUAL(getDigitalWriteValue(EN_PIN), LOW);
  END_IT
}

bool test_happy_path_plays_one_track() {
  IT("a queued track powers the module, sets volume and color, plays, then powers off");
  resetEnv();
  RgbLedWrapper rgbLed(19U, 7U);
  DFPlayer player(rgbLed, RX_PIN, TX_PIN, EN_PIN, BUSY_PIN);
  Task& task = player;
  player.play(7U, 20U, 1U, 2U, 3U);
  const uint32_t now = walkToPlaying(task);
  IS_EQUAL(getDigitalWriteValue(EN_PIN), HIGH);     // module powered during playback
  IS_EQUAL(packetCount(), 2U);
  IS_EQUAL(packetCmd(0U), CMD_VOLUME);
  IS_EQUAL(packetLsb(0U), 20U);
  IS_EQUAL(packetCmd(1U), CMD_PLAY);
  IS_EQUAL(packetMsb(1U), 0U);
  IS_EQUAL(packetLsb(1U), 7U);
  IS_EQUAL(LedStrip::lastColor.R, 1U);              // playback color shown
  IS_EQUAL(LedStrip::lastColor.G, 2U);
  IS_EQUAL(LedStrip::lastColor.B, 3U);
  finishTrack(task, now);
  IS_EQUAL(getDigitalWriteValue(EN_PIN), LOW);      // powered off after the queue drains
  IS_EQUAL(packetCount(), 2U);                      // no extra commands
  END_IT
}

bool test_sound_only_leaves_leds_untouched() {
  IT("an all-zero color request plays the track without overriding the LEDs");
  resetEnv();
  RgbLedWrapper rgbLed(19U, 7U);
  DFPlayer player(rgbLed, RX_PIN, TX_PIN, EN_PIN, BUSY_PIN);
  Task& task = player;
  rgbLed.setColor(9U, 8U, 7U, true);                // ambient color set before playback
  IS_EQUAL(LedStrip::clearToCount, 1);
  player.play(5U, 15U, 0U, 0U, 0U);
  const uint32_t now = walkToPlaying(task);
  IS_EQUAL(LedStrip::clearToCount, 1);              // SET_VOLUME skipped the LED override
  IS_EQUAL(LedStrip::lastColor.R, 9U);
  finishTrack(task, now);
  IS_EQUAL(LedStrip::clearToCount, 2);              // TURN_OFF re-applies the saved color
  IS_EQUAL(LedStrip::lastColor.R, 9U);
  IS_EQUAL(LedStrip::lastColor.G, 8U);
  IS_EQUAL(LedStrip::lastColor.B, 7U);
  END_IT
}

bool test_busy_timeout_sends_stop() {
  IT("a track that never signals completion is stopped after the play timeout");
  resetEnv();
  RgbLedWrapper rgbLed(19U, 7U);
  DFPlayer player(rgbLed, RX_PIN, TX_PIN, EN_PIN, BUSY_PIN);
  Task& task = player;
  player.play(7U, 20U, 1U, 2U, 3U);
  uint32_t now = walkToPlaying(task);
  step(task, now, 10001U);                          // WAIT_FOR_PLAY timeout -> stop()
  IS_EQUAL(packetCount(), 3U);
  IS_EQUAL(packetCmd(2U), CMD_STOP);
  step(task, now, 0U);                              // CHECK_QUEUE (empty) -> TURN_OFF
  step(task, now, 0U);                              // TURN_OFF -> IDLE
  IS_EQUAL(getDigitalWriteValue(EN_PIN), LOW);
  END_IT
}

bool test_queue_capacity_drops_sixth_track() {
  IT("the 5-deep play queue drops the sixth request and plays exactly five tracks");
  resetEnv();
  RgbLedWrapper rgbLed(19U, 7U);
  DFPlayer player(rgbLed, RX_PIN, TX_PIN, EN_PIN, BUSY_PIN);
  Task& task = player;
  for(uint16_t track = 1U; track <= 6U; ++track) {
    player.play(track, 10U, 1U, 1U, 1U);
  }
  uint32_t now = 0U;
  for(int i = 0; i < 60; ++i) {                     // generous pump: every wait state elapses
    triggerInterrupt(BUSY_PIN);
    step(task, now, 1100U);
  }
  size_t playCount = 0U;
  for(size_t i = 0U; i < packetCount(); ++i) {
    if(packetCmd(i) == CMD_PLAY) { ++playCount; }
  }
  IS_EQUAL(playCount, 5U);
  IS_EQUAL(getDigitalWriteValue(EN_PIN), LOW);      // drained and powered off
  END_IT
}

bool test_volume_is_masked_into_range() {
  IT("play() masks the volume to the 0-30 range before it reaches the module");
  resetEnv();
  RgbLedWrapper rgbLed(19U, 7U);
  DFPlayer player(rgbLed, RX_PIN, TX_PIN, EN_PIN, BUSY_PIN);
  Task& task = player;
  player.play(1U, 31U, 0U, 0U, 0U);                 // 31 & 30 = 30
  const uint32_t now = walkToPlaying(task);
  IS_EQUAL(packetCmd(0U), CMD_VOLUME);
  IS_EQUAL(packetLsb(0U), 30U);
  finishTrack(task, now);
  END_IT
}

int main() {
  SUITE("DFPlayer");
  test_idle_without_queue_does_nothing();
  test_happy_path_plays_one_track();
  test_sound_only_leaves_leds_untouched();
  test_busy_timeout_sends_stop();
  test_queue_capacity_drops_sixth_track();
  test_volume_is_masked_into_range();
  FINISH
}
