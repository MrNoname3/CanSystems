#ifndef DFPLAYER_HPP
#define DFPLAYER_HPP

#include <stdint.h>
#include <SoftwareSerial.h>                                         /// Arduino software serial lib.
#include "DFPlayerMiniFast.h"                                       /// DFPlayerMini driver lib.
#include "../../CircularBuffer/src/CircularBuffer.hpp"              /// Circular buffer class.
#include "../../rgbLedWrapper/src/rgbLedWrapper.hpp"                /// RGB LED controller class.

/// @brief Derived class for interacting with DFPlayerMini MP3 player with
/// playing queue and external turn on/off possibility with a PFET.
class DFPlayer final : private DFPlayerMiniFast {
private:
  struct __attribute__((packed))
  PlayQueueItem {
    uint16_t track;
    uint8_t volume;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    PlayQueueItem() : track(0), volume(0), red(0), green(0), blue(0) {}
    PlayQueueItem(uint16_t track, uint8_t volume, uint8_t red, uint8_t green, uint8_t blue) :
      track(track), volume(volume), red(red), green(green), blue(blue) {}
  };
public:
  /// @brief Constructor of DFPlayerMini derived class.
  /// @param RXpin_ Software serial RX pin.
  /// @param TXpin_ Software serial TX pin.
  /// @param ENpin Device turn on/off switch pin.
  /// @param INTpin Device playing interrupt pin: LOW->playing.
  /// @param timeout Set device answer timeout in ms.
  DFPlayer(RgbLedWrapper& rgbLed, uint8_t RXpin_, uint8_t TXpin_, uint8_t ENpin, uint8_t INTpin, uint32_t timeout = 10UL);

  /// @brief Destructor of the object.
  virtual ~DFPlayer() = default;

  /// @brief Add a new track to the playing queue.
  /// @param track Number of the track.
  /// @param volume Volume, value: 0-30.
  /// @param red Red color value of the LED strip.
  /// @param green Green color value of the LED strip.
  /// @param blue Blue color value of the LED strip.
  void play(uint16_t track, uint8_t volume, uint8_t red, uint8_t green, uint8_t blue);

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

  static constexpr uint16_t bootTime = 1000U;         // MP3 player boot time.
  static constexpr uint8_t cmdExecTime = 120U;        // Command execution time.
  static constexpr uint16_t playDelayTime = 400U;     // Time for delay between songs.
  static constexpr uint16_t playTimeoutTime = 10000U; // Playing timeout time.
  uint32_t bootTimer = 0UL;                           // MP3 player boot timer.
  uint32_t cmdExecTimer = 0UL;                        // Command execution timer.
  uint32_t playDelayTimer = 0UL;                      // Timer for delay between songs.
  uint32_t playTimeoutTimer = 0UL;                    // Playing timeout time.

  RgbLedWrapper& rgbLed;
  SoftwareSerial swSerial;                            // Software serial object.
  const uint8_t RXpin;                                // Software serial RX pin.
  const uint8_t TXpin;                                // Software serial TX pin.
  const uint8_t ENpin;                                // Device turn on/off switch pin.
  const uint8_t INTpin;                               // Device playing interrupt pin: LOW->playing.
  static volatile bool enablePlay;                    // Interrupt flag. If true, device is ready to play.

  PlayingStates playingState = PlayingStates::IDLE;   // Set state for state machine.
  CircularBuffer<PlayQueueItem, 5> playingQueue;      // MP3 playing queue.
};
#endif // DFPLAYER_HPP