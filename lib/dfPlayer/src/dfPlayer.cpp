#include "dfPlayer.hpp"
#include <Arduino.h>                                                /// Arduino libraries header.

volatile bool DFPlayer::enablePlay = false;                         // Set value for static variable.

DFPlayer::DFPlayer(RgbLedWrapper& rgbLed, uint8_t rxPin, uint8_t txPin, uint8_t enPin, uint8_t intPin, uint32_t timeout) :
  rgbLed(rgbLed),
  swSerial(rxPin, txPin),
  rxPin(rxPin),
  txPin(txPin),
  enPin(enPin),
  intPin(intPin),
  eventTimer(0U),
  playingState(PlayingStates::IDLE),
  playingQueue()
{
  swSerial.begin(9600);                                           // Open software serial port.
  pinMode(this->enPin, OUTPUT);                                   // Set pin modes.
  pinMode(this->intPin, INPUT_PULLUP);
  digitalWrite(this->enPin, LOW);                                 // Set pin states.
  digitalWrite(this->txPin, LOW);
  digitalWrite(this->rxPin, LOW);
  DFPlayerMiniFast::begin(swSerial, false, timeout);
}

void DFPlayer::play(uint16_t track, uint8_t volume, uint8_t red, uint8_t green, uint8_t blue) {
  track &= 9999U;                                                 // Protect variables from high value.
  volume &= 30U;
  if(!playingQueue.isFull()) {                                    // Put item to playing queue, if it is not full.
    playingQueue.put(PlayQueueItem(track, volume, red, green, blue));
  }
}

void DFPlayer::run() {
  const uint32_t actualTime = millis();
  switch(playingState) {
    case PlayingStates::IDLE: {
      if(!playingQueue.isEmpty()) {                               // Check playing queue.
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
      DFPlayerMiniFast::volume(playingQueue.peek().volume);       // Set volume trough base class.
      rgbLed.setColor(playingQueue.peek().red, playingQueue.peek().green, playingQueue.peek().blue, false);
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
      DFPlayerMiniFast::play(playingQueue.pop().track);           // Play next song from queue.
      eventTimer = actualTime;
      playingState = PlayingStates::WAIT_FOR_PLAY;
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
        playingState = PlayingStates::SET_VOLUME;
      }
    } break;
    case PlayingStates::TURN_OFF: {
      digitalWrite(enPin, LOW);                                   // Turn off device.
      digitalWrite(txPin, LOW);                                   // Set TX line in LOW state. (It's noisy.)
      digitalWrite(rxPin, LOW);                                   // Set RX line in LOW state. (It's noisy.)
      detachInt();                                                // Detach interrupt.
      enablePlay = false;                                         // Disable interrupt flag.
      rgbLed.loadColor();
      playingState = PlayingStates::IDLE;
    } break;
  }
}