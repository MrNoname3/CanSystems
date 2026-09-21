#include "dfPlayer.hpp"
#include <Arduino.h>                                                /// Arduino libraries header.

volatile bool DFPlayer::enablePlay = false;                         // Set value for static variable.

DFPlayer::DFPlayer(RgbLedWrapper& rgbLed, uint8_t rxPin, uint8_t txPin, uint8_t enPin, uint8_t intPin, uint16_t timeout) :
  rgbLed(rgbLed),
  swSerial(rxPin, txPin),
  rxPin(rxPin),
  txPin(txPin),
  enPin(enPin),
  intPin(intPin),
  eventTimer(0U),
  playingState(PlayingStates::IDLE),
  playFailedCallback(nullptr),
  playRetries(0U),
  moduleRestarts(0U) {
  swSerial.begin(9600);                                           // Open software serial port.
  pinMode(this->enPin, OUTPUT);                                   // Set pin modes.
  pinMode(this->intPin, INPUT_PULLUP);
  digitalWrite(this->enPin, LOW);                                 // Set pin states.
  digitalWrite(this->txPin, LOW);
  digitalWrite(this->rxPin, LOW);
  DFPlayerMiniFast<false>::begin(swSerial, timeout);
}

void DFPlayer::addPlayFailedCallback(void (*playFailedCallback)(uint16_t track)) {
  this->playFailedCallback = playFailedCallback;
}

void DFPlayer::play(uint16_t track, uint8_t volume, uint8_t red, uint8_t green, uint8_t blue) {
  if(track > maxTrack) { track = maxTrack; }
  if(volume > maxVolume) { volume = maxVolume; }
  if(!playingQueue.isFull()) {                                    // Put item to playing queue, if it is not full.
    playingQueue.put(PlayQueueItem(track, volume, red, green, blue));
  }
}

void DFPlayer::powerDownModule() {
  digitalWrite(enPin, LOW);                                     // Turn off device.
  digitalWrite(txPin, LOW);                                     // Set TX line in LOW state. (It's noisy.)
  digitalWrite(rxPin, LOW);                                     // Set RX line in LOW state. (It's noisy.)
  detachInt();                                                  // Detach interrupt.
}

DFPlayer::PlayingStates DFPlayer::nextStateWaitingForStart(uint32_t actualTime) {
  // The play command goes out with no feedback requested, so BUSY is the only word the module
  // gives: low means the track started. A track shorter than one task round is over before it can
  // be seen low, and then the end-of-play interrupt is the proof instead.
  if(enablePlay) {
    enablePlay = false;
    return PlayingStates::CHECK_QUEUE;
  }
  if(digitalRead(intPin) == LOW) {
    eventTimer = actualTime;
    return PlayingStates::WAIT_FOR_PLAY;
  }
  if(!Time::hasElapsed(actualTime, eventTimer, playStartTime)) {
    return PlayingStates::WAIT_FOR_START;
  }
  // Nothing started, so the command was lost on the way. Send it again; if the module stays deaf,
  // power-cycle it, which is what clears a wedged module; if it is still deaf, the track is gone.
  if(playRetries < playStartRetries) {
    playRetries++;
    return PlayingStates::PLAY;
  }
  if(moduleRestarts < moduleRestartLimit) {
    moduleRestarts++;
    playRetries = 0U;
    powerDownModule();
    eventTimer = actualTime;
    return PlayingStates::RESTART_MODULE;
  }
  if(playFailedCallback != nullptr) { playFailedCallback(currentItem.track); }
  return PlayingStates::CHECK_QUEUE;
}

bool DFPlayer::run() {
  const uint32_t actualTime = millis();
  switch(playingState) {
    case PlayingStates::IDLE: {
      if(!playingQueue.isEmpty()) {                               // Check playing queue.
        currentItem = playingQueue.pop();
        playRetries = 0U;
        moduleRestarts = 0U;
        playingState = PlayingStates::TURN_ON;
      }
    } break;
    case PlayingStates::TURN_ON: {
      digitalWrite(txPin, HIGH);                                  // Set TX line in HIGH state.
      digitalWrite(rxPin, HIGH);                                  // Set RX line in HIGH state.
      digitalWrite(enPin, HIGH);                                  // Turn on device.
      eventTimer = actualTime;
      playingState = PlayingStates::WAIT_FOR_BOOT;
    } break;
    case PlayingStates::WAIT_FOR_BOOT: {
      if(Time::hasElapsed(actualTime, eventTimer, bootTime)) {
        playingState = PlayingStates::SET_VOLUME;
      }
    } break;
    case PlayingStates::SET_VOLUME: {
      DFPlayerMiniFast::volume(currentItem.volume);              // Set volume trough base class.
      // An all-zero color means a sound-only request: leave the LEDs unchanged during playback
      // instead of forcing them dark. The unconditional loadColor() in TURN_OFF stays harmless,
      // it just re-applies the already-active saved color.
      if((currentItem.red | currentItem.green | currentItem.blue) != 0U) {
        rgbLed.setColor(currentItem.red, currentItem.green, currentItem.blue, false);
      }
      eventTimer = actualTime;
      playingState = PlayingStates::WAIT_FOR_CMD;
    } break;
    case PlayingStates::WAIT_FOR_CMD: {
      if(Time::hasElapsed(actualTime, eventTimer, cmdExecTime)) {
        playingState = PlayingStates::PLAY;
      }
    } break;
    case PlayingStates::PLAY: {
      attachInt();
      enablePlay = false;                                         // Any edge from here on is this track's.
      DFPlayerMiniFast::play(currentItem.track);                  // Play the track being handled.
      eventTimer = actualTime;
      playingState = PlayingStates::WAIT_FOR_START;
    } break;
    case PlayingStates::WAIT_FOR_START: {
      playingState = nextStateWaitingForStart(actualTime);
    } break;
    case PlayingStates::RESTART_MODULE: {
      if(Time::hasElapsed(actualTime, eventTimer, moduleRestartTime)) {
        playingState = PlayingStates::TURN_ON;
      }
    } break;
    case PlayingStates::WAIT_FOR_PLAY: {
      if(enablePlay) {                                            // Wait for interrupt.
        enablePlay = false;
        playingState = PlayingStates::CHECK_QUEUE;
      }
      if(Time::hasElapsed(actualTime, eventTimer, playTimeoutTime)) {
        DFPlayerMiniFast::stop();                                 // Stop playing.
        playingState = PlayingStates::CHECK_QUEUE;
      }
    } break;
    case PlayingStates::CHECK_QUEUE: {
      if(playingQueue.isEmpty()) {                               // Check playing queue.
        playingState = PlayingStates::TURN_OFF;
      } else {
        eventTimer = actualTime;
        playingState = PlayingStates::PLAYING_DELAY;
      }
    } break;
    case PlayingStates::PLAYING_DELAY: {
      if(Time::hasElapsed(actualTime, eventTimer, playDelayTime)) {
        enablePlay = false;                                       // Disable interrupt flag.
        currentItem = playingQueue.pop();                         // The module stays on for this one.
        playRetries = 0U;
        moduleRestarts = 0U;
        playingState = PlayingStates::SET_VOLUME;
      }
    } break;
    case PlayingStates::TURN_OFF: {
      powerDownModule();
      enablePlay = false;                                         // Disable interrupt flag.
      rgbLed.loadColor();
      playingState = PlayingStates::IDLE;
    } break;
  }
  return true;
}
