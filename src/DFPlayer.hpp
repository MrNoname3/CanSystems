#ifndef __DFPLAYER_HPP__
#define __DFPLAYER_HPP__
#include <SoftwareSerial.h>
#include "DFPlayerMiniFast.h"
#include "CircularBuffer.hpp"               /// Circular buffer class.


class DFPlayer : private DFPlayerMiniFast {

public:

  DFPlayer(uint8_t RXpin_, uint8_t TXpin_, uint8_t ENpin, uint8_t INTpin, 
    bool debug = false, uint32_t threshold = 10) : swSerial(RXpin_, TXpin_) {

    this->RXpin = RXpin_;
    this->TXpin = TXpin_;  
    this->ENpin = ENpin;
    this->INTpin = INTpin;
    swSerial.begin(9600);
    pinMode(this->ENpin, OUTPUT);
    pinMode(this->INTpin, INPUT_PULLUP);
    DFPlayerMiniFast::begin(swSerial, debug, threshold);
  }

  ~DFPlayer() {
    
  }

  void volume(uint8_t vol) {
    this->vol = vol;
  }

  void play(uint16_t song) {
    playingQueue.put(song);
  }

  void looping(void) {

    switch(playingState) {
      
      case PlayingStates::CHECK_QUEUE_TO_BEGIN: {
        if(playingQueue.isEmpty() == false) {   
          Serial.println("CHECK_QUEUE_TO_BEGIN");
          playingState = PlayingStates::TURN_ON;          
        }
      } break;

      case PlayingStates::TURN_ON: {
        Serial.println("TURN_ON");
        digitalWrite(this->ENpin, HIGH);
        digitalWrite(this->TXpin, HIGH);
        bootTimer = millis();
        playingState = PlayingStates::WAIT_FOR_BOOT;
      } break;

      case PlayingStates::WAIT_FOR_BOOT: {
        if(millis()- bootTimer >= bootTime) {
          Serial.println("WAIT_FOR_BOOT");
          playingState = PlayingStates::SET_VOLUME;
        }
      } break;

      case PlayingStates::SET_VOLUME: {
        Serial.println("SET_VOLUME");
        DFPlayerMiniFast::volume(vol);
        setVolumeTimer = millis();
        playingState = PlayingStates::WAIT_FOR_SET_VOLUME;
      } break;

      case PlayingStates::WAIT_FOR_SET_VOLUME: {
        if(millis() - setVolumeTimer >= setVolumeTime) {
          Serial.println("WAIT_FOR_SET_VOLUME");
          playingState = PlayingStates::PLAY;
        }
      } break;

      case PlayingStates::PLAY: {
        Serial.println("PLAY");
        this->attachInt();      
        DFPlayerMiniFast::play(playingQueue.pop());
        playTimeoutTimer = millis();
        playingState = PlayingStates::WAIT_FOR_PLAY;
      } break;

      case PlayingStates::WAIT_FOR_PLAY: {        
        if(enablePlay == true) {
          Serial.println("WAIT_FOR_PLAY");
          enablePlay = false;
          playingState = PlayingStates::CHECK_QUEUE_TO_RESUME;
        }
        if(millis() - playTimeoutTimer >= playTimeoutTime) {
          Serial.println("TIMEOUT");
          DFPlayerMiniFast::stop();
          playingState = PlayingStates::CHECK_QUEUE_TO_RESUME;
        }
      } break;

      case PlayingStates::CHECK_QUEUE_TO_RESUME: {        
        Serial.println("CHECK_QUEUE_TO_RESUME");
        if(playingQueue.isEmpty() == false) {
          playDelayTimer = millis();
          playingState = PlayingStates::PLAYING_DELAY;          
        }
        else {
          playingState = PlayingStates::TURN_OFF;
        }
      } break;

      case PlayingStates::PLAYING_DELAY: {       
        if(millis() - playDelayTimer >= playDelayTime) {
          Serial.println("PLAYING_DELAY");
          enablePlay = false;
          playingState = PlayingStates::PLAY;
        }
      } break;

      case PlayingStates::TURN_OFF: {
        Serial.println("TURN_OFF");
        digitalWrite(this->ENpin, LOW);
        digitalWrite(this->TXpin, LOW);
        detachInt();
        enablePlay = false;
        playingState = PlayingStates::CHECK_QUEUE_TO_BEGIN;
      } break;      

    }
  }

private:

  static void irqHandler(void) {
    enablePlay = true;
  }

  void attachInt(void) {
    bitSet(EIFR, digitalPinToInterrupt(INTpin));
    attachInterrupt(digitalPinToInterrupt(INTpin), irqHandler, RISING);
  }

  void detachInt(void) {
    detachInterrupt(digitalPinToInterrupt(INTpin));
  }

  enum class PlayingStates : uint8_t {
    CHECK_QUEUE_TO_BEGIN,   // Check playing queue.    
    TURN_ON,                // Turn hardware on.
    WAIT_FOR_BOOT,          // Wait a little for hardware boot.
    SET_VOLUME,             // Set the device playing volume.
    WAIT_FOR_SET_VOLUME,    // Waiting a bit for command execution.
    PLAY,                   // Play a song.
    WAIT_FOR_PLAY,
    CHECK_QUEUE_TO_RESUME,
    PLAYING_DELAY,
    TURN_OFF,               // Turn hardware off.
  };

  SoftwareSerial swSerial;
  uint8_t RXpin = 0;
  uint8_t TXpin = 0;
  uint8_t ENpin = 0;
  uint8_t INTpin = 0;
  uint8_t vol = 5;
  
  static volatile bool enablePlay;
  PlayingStates playingState = PlayingStates::CHECK_QUEUE_TO_BEGIN;
  const uint16_t bootTime = 1000;
  uint32_t bootTimer = 0;
  const uint8_t setVolumeTime = 10;
  uint32_t setVolumeTimer = 0;
  const uint16_t playDelayTime = 500;
  uint32_t playDelayTimer = 0;
  const uint16_t playTimeoutTime = 10000;
  uint32_t playTimeoutTimer = 0;
  CircularBuffer<uint16_t, 10> playingQueue;

};

volatile bool DFPlayer::enablePlay = false;

#endif