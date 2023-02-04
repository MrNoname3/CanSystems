#ifndef DFPLAYER_HPP
#define DFPLAYER_HPP

#include <SoftwareSerial.h>                 /// Arduino software serial lib.
#include "DFPlayerMiniFast.h"               /// DFPlayerMini driver lib.
#include "CircularBuffer.hpp"               /// Circular buffer class.

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
  void attachRGBController(void (*RGBController)(uint8_t, uint8_t, uint8_t));

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

  const uint16_t bootTime = 1000;           // MP3 player boot time.
  const uint8_t cmdExecTime = 60;           // Command execution time.
  const uint16_t playDelayTime = 500;       // Time for delay between songs.
  const uint16_t playTimeoutTime = 10000;   // Playing timeout time.
  uint32_t bootTimer = 0;                   // MP3 player boot timer.
  uint32_t cmdExecTimer = 0;                // Command execution timer.
  uint32_t playDelayTimer = 0;              // Timer for delay between songs.
  uint32_t playTimeoutTimer = 0;            // Playing timeout time.

  SoftwareSerial swSerial;                  // Software serial object.
  uint8_t RXpin = 0;                        // Software serial RX pin.
  uint8_t TXpin = 0;                        // Software serial TX pin.
  uint8_t ENpin = 0;                        // Device turn on/off switch pin.
  uint8_t INTpin = 0;                       // Device playing interrupt pin: LOW->playing.
  uint8_t volume_ = 5;                      // Volume value. Default: 5.
  bool debug = false;                       // Enable debug prints.
  static volatile bool enablePlay;          // Interrupt flag. If true, device is ready to play.

  PlayingStates playingState = PlayingStates::IDLE;             // Set state for state machine.
  CircularBuffer<uint16_t, 10> playingQueue;                    // MP3 playing queue.
  void (*RGBController)(uint8_t, uint8_t, uint8_t) = nullptr;   // RGB LED controller function pointer.

};  // End of class definition.

volatile bool DFPlayer::enablePlay = false;           // Set value for static variable.

DFPlayer::DFPlayer(uint8_t RXpin_, uint8_t TXpin_, uint8_t ENpin, uint8_t INTpin,
  bool debug, uint32_t timeout) : swSerial(RXpin_, TXpin_) {

  this->RXpin = RXpin_;                               // Save given pin numbers.
  this->TXpin = TXpin_;
  this->ENpin = ENpin;
  this->INTpin = INTpin;
  this->debug = debug;                                // Set debug value.
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

void DFPlayer::attachRGBController(void (*RGBController)(uint8_t, uint8_t, uint8_t)) {
  this->RGBController = RGBController;                // Store controller function pointer locally.
}

void DFPlayer::detachRGBController() {
  this->RGBController = NULL;                         // Clear locally stored controller function pointer.
}

void DFPlayer::spin() {

  switch(playingState) {

    case PlayingStates::IDLE: {
      if(playingQueue.isEmpty() == false) {                     // Check playing queue.
        if(debug == true) { Serial.println(F("IDLE")); }        // Debug print.
        if(RGBController != NULL) { RGBController(0, 50, 200); }  // Set RGB LED color.
        playingState = PlayingStates::TURN_ON;                  // Set next state.
      }
    } break;

    case PlayingStates::TURN_ON: {
      if(debug == true) { Serial.println(F("TURN_ON")); }       // Debug print.      
      digitalWrite(this->TXpin, HIGH);                          // Set TX line in HIGH state.
      digitalWrite(this->RXpin, HIGH);                          // Set RX line in HIGH state.
      digitalWrite(this->ENpin, HIGH);                          // Turn on device.
      bootTimer = millis();                                     // Start timer.
      playingState = PlayingStates::WAIT_FOR_BOOT;              // Set next state.
    } break;

    case PlayingStates::WAIT_FOR_BOOT: {
      if(millis()- bootTimer >= bootTime) {                     // Check timer.
        if(debug == true) { Serial.println(F("WAIT_FOR_BOOT")); }  // Debug print.
        playingState = PlayingStates::SET_VOLUME;               // Set next state.
      }
    } break;

    case PlayingStates::SET_VOLUME: {
      if(debug == true) { Serial.println(F("SET_VOLUME")); }    // Debug print.
      DFPlayerMiniFast::volume(volume_);                        // Set volume trough base class.
      cmdExecTimer = millis();                                  // Start timer.
      playingState = PlayingStates::WAIT_FOR_CMD;               // Set next state.
    } break;

    case PlayingStates::WAIT_FOR_CMD: {
      if(millis() - cmdExecTimer >= cmdExecTime) {              // Check timer.
        if(debug == true) { Serial.println(F("WAIT_FOR_CMD")); }  // Debug print.
        playingState = PlayingStates::PLAY;                     // Set next state.
      }
    } break;

    case PlayingStates::PLAY: {
      if(debug == true) { Serial.println(F("PLAY")); }          // Debug print.
      this->attachInt();                                        // Attach interrupt.
      DFPlayerMiniFast::play(playingQueue.pop());               // Play next song from queue.
      playTimeoutTimer = millis();                              // Start timer.
      playingState = PlayingStates::WAIT_FOR_PLAY;              // Set next state.
    } break;

    case PlayingStates::WAIT_FOR_PLAY: {
      if(enablePlay == true) {                                  // Wait for interrupt.
        if(debug == true) { Serial.println(F("WAIT_FOR_PLAY")); }  // Debug print.
        enablePlay = false;                                     // Disable interrupt flag.
        playingState = PlayingStates::CHECK_QUEUE;              // Set next state.
      }
      if(millis() - playTimeoutTimer >= playTimeoutTime) {      // Check timeout timer.
        if(debug == true) { Serial.println(F("TIMEOUT")); }     // Debug print.
        DFPlayerMiniFast::stop();                               // Stop playing.
        playingState = PlayingStates::CHECK_QUEUE;              // Set next state.
      }
    } break;

    case PlayingStates::CHECK_QUEUE: {
      if(debug == true) { Serial.println(F("CHECK_QUEUE")); }   // Debug print.
      if(playingQueue.isEmpty() == false) {                     // Check playing queue.
        playDelayTimer = millis();                              // Start timer.
        playingState = PlayingStates::PLAYING_DELAY;            // Set next state.
      }
      else {
        playingState = PlayingStates::TURN_OFF;                 // Set next state.
      }
    } break;

    case PlayingStates::PLAYING_DELAY: {
      if(millis() - playDelayTimer >= playDelayTime) {          // Check timer.
        if(debug == true) { Serial.println(F("PLAYING_DELAY")); }  // Debug print.
        enablePlay = false;                                     // Disable interrupt flag.
        playingState = PlayingStates::PLAY;                     // Set next state.
      }
    } break;

    case PlayingStates::TURN_OFF: {
      if(debug == true) { Serial.println(F("TURN_OFF")); }      // Debug print.
      digitalWrite(this->ENpin, LOW);                           // Turn on device.
      digitalWrite(this->TXpin, LOW);                           // Set TX line in LOW state. (It's noisy.)
      digitalWrite(this->RXpin, LOW);                           // Set RX line in LOW state. (It's noisy.)
      detachInt();                                              // Detach interrupt.
      enablePlay = false;                                       // Disable interrupt flag.
      if(RGBController != NULL) { RGBController(0, 0, 0); }     // Set RGB LED color.
      playingState = PlayingStates::IDLE;                       // Set next state.
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