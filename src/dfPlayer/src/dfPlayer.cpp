#include "dfPlayer.hpp"
#include <Arduino.h>                                                /// Arduino libraries header.

volatile bool DFPlayer::enablePlay = false;           // Set value for static variable.

DFPlayer::DFPlayer(RgbLedWrapper& rgbLed, uint8_t RXpin, uint8_t TXpin, uint8_t ENpin, uint8_t INTpin, uint32_t timeout) : 
  rgbLed(rgbLed),
  swSerial(RXpin, TXpin),
  RXpin(RXpin),
  TXpin(TXpin),
  ENpin(ENpin),
  INTpin(INTpin)
{
  swSerial.begin(9600);                               // Open software serial port.
  pinMode(this->ENpin, OUTPUT);                       // Set pin modes.
  pinMode(this->INTpin, INPUT_PULLUP);
  digitalWrite(this->ENpin, LOW);                     // Set pin states.
  digitalWrite(this->TXpin, LOW);
  digitalWrite(this->RXpin, LOW);
  DFPlayerMiniFast::begin(swSerial, false, timeout);
}

void DFPlayer::volume(uint8_t volume_) {
  volume_ &= 30;                                      // Protect variable from high value.
  this->volume_ = volume_;                            // Save value.
}

void DFPlayer::play(uint16_t song) {
  song &= 9999;                                       // Protect variable from high value.
  this->playingQueue.put(song);                       // Put value to playing queue.
}

void DFPlayer::spin() {

  switch(playingState) {

    case PlayingStates::IDLE: {
      if(playingQueue.isEmpty() == false) {                     // Check playing queue.
        rgbLed.setColor(redValue, greenValue, blueValue);
        playingState = PlayingStates::TURN_ON;
      }
    } break;

    case PlayingStates::TURN_ON: {
      digitalWrite(this->TXpin, HIGH);                          // Set TX line in HIGH state.
      digitalWrite(this->RXpin, HIGH);                          // Set RX line in HIGH state.
      digitalWrite(this->ENpin, HIGH);                          // Turn on device.
      bootTimer = millis();                                     // Start timer.
      playingState = PlayingStates::WAIT_FOR_BOOT;
    } break;

    case PlayingStates::WAIT_FOR_BOOT: {
      if(millis()- bootTimer >= bootTime) {                     // Check timer.
        playingState = PlayingStates::SET_VOLUME;
      }
    } break;

    case PlayingStates::SET_VOLUME: {
      DFPlayerMiniFast::volume(volume_);                        // Set volume trough base class.
      cmdExecTimer = millis();                                  // Start timer.
      playingState = PlayingStates::WAIT_FOR_CMD;
    } break;

    case PlayingStates::WAIT_FOR_CMD: {
      if(millis() - cmdExecTimer >= cmdExecTime) {              // Check timer.
        playingState = PlayingStates::PLAY;
      }
    } break;

    case PlayingStates::PLAY: {
      this->attachInt();                                        // Attach interrupt.
      DFPlayerMiniFast::play(playingQueue.pop());               // Play next song from queue.
      playTimeoutTimer = millis();                              // Start timer.
      playingState = PlayingStates::WAIT_FOR_PLAY;
    } break;

    case PlayingStates::WAIT_FOR_PLAY: {
      if(enablePlay) {                                          // Wait for interrupt.
        enablePlay = false;                                     // Disable interrupt flag.
        playingState = PlayingStates::CHECK_QUEUE;
      }
      if(millis() - playTimeoutTimer >= playTimeoutTime) {      // Check timeout timer.
        DFPlayerMiniFast::stop();                               // Stop playing.
        playingState = PlayingStates::CHECK_QUEUE;
      }
    } break;

    case PlayingStates::CHECK_QUEUE: {
      if(playingQueue.isEmpty() == false) {                     // Check playing queue.
        playDelayTimer = millis();                              // Start timer.
        playingState = PlayingStates::PLAYING_DELAY;
      }
      else {
        playingState = PlayingStates::TURN_OFF;
      }
    } break;

    case PlayingStates::PLAYING_DELAY: {
      if(millis() - playDelayTimer >= playDelayTime) {          // Check timer.
        enablePlay = false;                                     // Disable interrupt flag.
        playingState = PlayingStates::PLAY;
      }
    } break;

    case PlayingStates::TURN_OFF: {
      digitalWrite(this->ENpin, LOW);                           // Turn on device.
      digitalWrite(this->TXpin, LOW);                           // Set TX line in LOW state. (It's noisy.)
      digitalWrite(this->RXpin, LOW);                           // Set RX line in LOW state. (It's noisy.)
      detachInt();                                              // Detach interrupt.
      enablePlay = false;                                       // Disable interrupt flag.
      rgbLed.clear();
      playingState = PlayingStates::IDLE;
    } break;

  }
}

void DFPlayer::irqHandler() {
  enablePlay = true;                                            // Set interrupt flag variable.
}

void DFPlayer::attachInt() const {
  // Clear interrupt flag, because it stores the interrupt event, even it is not attached.
  bitSet(EIFR, digitalPinToInterrupt(INTpin));
  // Attach interrupt to the given pin.
  attachInterrupt(digitalPinToInterrupt(INTpin), irqHandler, RISING);
}

void DFPlayer::detachInt() const {
  // Detach interrupt from the given pin.
  detachInterrupt(digitalPinToInterrupt(INTpin));
}