#ifndef DFPLAYER_HPP
#define DFPLAYER_HPP

#include <Arduino.h>                          /// Arduino libraries header.
#include <SoftwareSerial.h>                   /// Arduino software serial lib.
#include "DFPlayerMiniFast.h"                 /// DFPlayerMini driver lib.
#include "CircularBuffer.hpp"                 /// Circular buffer class.

/// @brief Derived class for interacting with DFPlayerMini MP3 player with
/// playing queue and external turn on/off possibility with a PFET.
class DFPlayer : private DFPlayerMiniFast {

public:

  /// @brief Constructor of DFPlayerMini derived class.
  /// @param RXpin_ Software serial RX pin.
  /// @param TXpin_ Software serial TX pin.
  /// @param ENpin Device turn on/off switch pin.
  /// @param INTpin Device playing interrupt pin: LOW->playing.
  /// @param debug Enable debug prints.
  /// @param timeout Set device answer timeout in ms.
  DFPlayer(uint8_t RXpin_, uint8_t TXpin_, uint8_t ENpin, uint8_t INTpin,
    bool debug = false, uint32_t timeout = 10);

  /// @brief Destructor of the object.
  virtual ~DFPlayer() = default;

  /// @brief Set MP3 player volume.
  /// @param volume_ Volume, value: 0-30.
  void volume(uint8_t volume_);

  /// @brief Put song number in the play queue.
  /// @param song Number of the song.
  void play(uint16_t song);

  /// @brief Attach an RGB LED controller function to use with MP3 player.
  /// @param RGBController Pointer of the RGB LED controller function.
  void attachRGBController(void (*RGBController)(const uint8_t, const uint8_t, const uint8_t));

  /// @brief Detach the RGB LED controller function.
  void detachRGBController();

  /// @brief Handles the state machine for playing.
  /// Need to be called periodically.
  void spin();

  /// @brief Print occured errors.
  using DFPlayerMiniFast::printError;

  DFPlayer(const DFPlayer&) = delete;                       // Define copy constructor.
  DFPlayer& operator=(const DFPlayer&) = delete;            // Define copy assignment operator.
  DFPlayer(DFPlayer&&) = delete;                            // Define move constructor.
  DFPlayer& operator=(DFPlayer&&) = delete;                 // Define move assignment operator.

private:

  /// @brief Attach interrupt to the given interrupt pin.
  void attachInt() const ;

  /// @brief Detach interrupt from the given interrupt pin.
  void detachInt() const ;

  /// @brief Handles interrupt.
  static void irqHandler();

  /// @brief State machine states for playing.
  enum class PlayingStates : uint8_t {
    IDLE,                           // Check playing queue to begin playing.
    TURN_ON,                        // Turn hardware on.
    WAIT_FOR_BOOT,                  // Wait a little for hardware boot.
    SET_VOLUME,                     // Set the device playing volume.
    WAIT_FOR_CMD,                   // Waiting a bit for command execution.
    PLAY,                           // Play a song.
    WAIT_FOR_PLAY,                  // Wait for end of playing.
    CHECK_QUEUE,                    // Check play queue.
    PLAYING_DELAY,                  // Delay between songs.
    TURN_OFF,                       // Turn hardware off.
  };

  static const uint16_t bootTime = 1000;            // MP3 player boot time.
  static const uint8_t cmdExecTime = 120;           // Command execution time.
  static const uint16_t playDelayTime = 500;        // Time for delay between songs.
  static const uint16_t playTimeoutTime = 10000;    // Playing timeout time.
  uint32_t bootTimer = 0;                           // MP3 player boot timer.
  uint32_t cmdExecTimer = 0;                        // Command execution timer.
  uint32_t playDelayTimer = 0;                      // Timer for delay between songs.
  uint32_t playTimeoutTimer = 0;                    // Playing timeout time.

  SoftwareSerial swSerial;                          // Software serial object.
  const uint8_t RXpin;                              // Software serial RX pin.
  const uint8_t TXpin;                              // Software serial TX pin.
  const uint8_t ENpin;                              // Device turn on/off switch pin.
  const uint8_t INTpin;                             // Device playing interrupt pin: LOW->playing.
  uint8_t volume_ = 5;                              // Volume value. Default: 5.
  bool debug = false;                               // Enable debug prints.
  static volatile bool enablePlay;                  // Interrupt flag. If true, device is ready to play.

  PlayingStates playingState = PlayingStates::IDLE; // Set state for state machine.
  CircularBuffer<uint16_t, 5> playingQueue;         // MP3 playing queue.
  void (*RGBController)(const uint8_t, const uint8_t, const uint8_t) = nullptr;   // RGB LED controller function pointer.

};  // End of class definition.

volatile bool DFPlayer::enablePlay = false;           // Set value for static variable.

DFPlayer::DFPlayer(uint8_t RXpin, uint8_t TXpin, uint8_t ENpin, uint8_t INTpin, bool debug, uint32_t timeout) : 
  swSerial(RXpin, TXpin), RXpin(RXpin), TXpin(TXpin), ENpin(ENpin), INTpin(INTpin), debug(debug) {
  swSerial.begin(9600);                               // Open software serial port.
  pinMode(this->ENpin, OUTPUT);                       // Set pin modes.
  pinMode(this->INTpin, INPUT_PULLUP);
  digitalWrite(this->ENpin, LOW);                     // Set pin states.
  digitalWrite(this->TXpin, LOW);
  digitalWrite(this->RXpin, LOW);
  DFPlayerMiniFast::begin(swSerial, debug, timeout);  // Call base class constructor.
}

void DFPlayer::volume(uint8_t volume_) {
  volume_ &= 30;                                      // Protect variable from high value.
  this->volume_ = volume_;                            // Save value.
}

void DFPlayer::play(uint16_t song) {
  song &= 9999;                                       // Protect variable from high value.
  this->playingQueue.put(song);                       // Put value to playing queue.
}

void DFPlayer::attachRGBController(void (*RGBController)(const uint8_t, const uint8_t, const uint8_t)) {
  this->RGBController = RGBController;                // Store controller function pointer locally.
}

void DFPlayer::detachRGBController() {
  this->RGBController = NULL;                         // Clear locally stored controller function pointer.
}

void DFPlayer::spin() {

  switch(playingState) {

    case PlayingStates::IDLE: {
      if(playingQueue.isEmpty() == false) {                     // Check playing queue.
        if(debug) { Serial.println(F("IDLE")); }
        if(RGBController != NULL) { RGBController(0, 50, 200); }  // Set RGB LED color.
        playingState = PlayingStates::TURN_ON;
      }
    } break;

    case PlayingStates::TURN_ON: {
      if(debug) { Serial.println(F("TURN_ON")); }
      digitalWrite(this->TXpin, HIGH);                          // Set TX line in HIGH state.
      digitalWrite(this->RXpin, HIGH);                          // Set RX line in HIGH state.
      digitalWrite(this->ENpin, HIGH);                          // Turn on device.
      bootTimer = millis();                                     // Start timer.
      playingState = PlayingStates::WAIT_FOR_BOOT;
    } break;

    case PlayingStates::WAIT_FOR_BOOT: {
      if(millis()- bootTimer >= bootTime) {                     // Check timer.
        if(debug) { Serial.println(F("WAIT_FOR_BOOT")); }
        playingState = PlayingStates::SET_VOLUME;
      }
    } break;

    case PlayingStates::SET_VOLUME: {
      if(debug) { Serial.println(F("SET_VOLUME")); }
      DFPlayerMiniFast::volume(volume_);                        // Set volume trough base class.
      cmdExecTimer = millis();                                  // Start timer.
      playingState = PlayingStates::WAIT_FOR_CMD;
    } break;

    case PlayingStates::WAIT_FOR_CMD: {
      if(millis() - cmdExecTimer >= cmdExecTime) {              // Check timer.
        if(debug) { Serial.println(F("WAIT_FOR_CMD")); }
        playingState = PlayingStates::PLAY;
      }
    } break;

    case PlayingStates::PLAY: {
      if(debug) { Serial.println(F("PLAY")); }
      this->attachInt();                                        // Attach interrupt.
      DFPlayerMiniFast::play(playingQueue.pop());               // Play next song from queue.
      playTimeoutTimer = millis();                              // Start timer.
      playingState = PlayingStates::WAIT_FOR_PLAY;
    } break;

    case PlayingStates::WAIT_FOR_PLAY: {
      if(enablePlay) {                                          // Wait for interrupt.
        if(debug) { Serial.println(F("WAIT_FOR_PLAY")); }
        enablePlay = false;                                     // Disable interrupt flag.
        playingState = PlayingStates::CHECK_QUEUE;
      }
      if(millis() - playTimeoutTimer >= playTimeoutTime) {      // Check timeout timer.
        if(debug) { Serial.println(F("TIMEOUT")); }
        DFPlayerMiniFast::stop();                               // Stop playing.
        playingState = PlayingStates::CHECK_QUEUE;
      }
    } break;

    case PlayingStates::CHECK_QUEUE: {
      if(debug) { Serial.println(F("CHECK_QUEUE")); }
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
        if(debug) { Serial.println(F("PLAYING_DELAY")); }
        enablePlay = false;                                     // Disable interrupt flag.
        playingState = PlayingStates::PLAY;
      }
    } break;

    case PlayingStates::TURN_OFF: {
      if(debug) { Serial.println(F("TURN_OFF")); }
      digitalWrite(this->ENpin, LOW);                           // Turn on device.
      digitalWrite(this->TXpin, LOW);                           // Set TX line in LOW state. (It's noisy.)
      digitalWrite(this->RXpin, LOW);                           // Set RX line in LOW state. (It's noisy.)
      detachInt();                                              // Detach interrupt.
      enablePlay = false;                                       // Disable interrupt flag.
      if(RGBController != NULL) { RGBController(0, 0, 0); }     // Set RGB LED color.
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


#endif // DFPLAYER_HPP