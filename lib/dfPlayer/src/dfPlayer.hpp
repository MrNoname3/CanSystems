#ifndef DFPLAYER_HPP
#define DFPLAYER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <SoftwareSerial.h>                                         /// Arduino software serial lib.
#include "DFPlayerMiniFast.h"                                       /// DFPlayerMini driver lib.
#include "CircularBuffer.hpp"                                       /// Circular buffer class.
#include "rgbLedWrapper.hpp"                                        /// RGB LED controller class.
#include "taskHandler.hpp"                                          /// Class for task scheduling.
#include "common.hpp"                                               /// Common definitions and functions.

/// @brief Handles interaction with a DFPlayerMini MP3 player.
/// Includes a track-playing queue and external power control via a PFET.
class DFPlayer final : private DFPlayerMiniFast<false>, public Task {
private:
  /// @brief Represents a single item in the play queue.
  struct __attribute__((packed))
  PlayQueueItem {
    uint16_t track;                 // Track number.
    uint8_t volume;                 // Volume level (0-30).
    uint8_t red;                    // Red LED color component.
    uint8_t green;                  // Green LED color component.
    uint8_t blue;                   // Blue LED color component.

    /// @brief Default constructor.
    PlayQueueItem() :
      track(0U),
      volume(0U),
      red(0U),
      green(0U),
      blue(0U) {}

    /// @brief Constructs a PlayQueueItem with specified parameters.
    /// @param track The track number to be played (1-9999). Values above 9999 will be capped.
    /// @param volume The playback volume (0-30). Values above 30 will be capped.
    /// @param red The red color component for the LED strip (0-255).
    /// @param green The green color component for the LED strip (0-255).
    /// @param blue The blue color component for the LED strip (0-255).
    PlayQueueItem(uint16_t track, uint8_t volume, uint8_t red, uint8_t green, uint8_t blue) :
      track(track),
      volume(volume),
      red(red),
      green(green),
      blue(blue) {}
  };

public:
  /// @brief Constructs the DFPlayer class.
  /// @param rgbLed Reference to the RGB LED controller.
  /// @param rxPin Software serial RX pin.
  /// @param txPin Software serial TX pin.
  /// @param enPin Device power control pin.
  /// @param intPin Device interrupt pin (LOW when playing).
  /// @param timeout Response timeout for commands (ms).
  DFPlayer(RgbLedWrapper& rgbLed, uint8_t rxPin, uint8_t txPin, uint8_t enPin, uint8_t intPin, uint16_t timeout = 10U);

  /// @brief Default destructor.
  ~DFPlayer() override = default;

  /// @brief Adds a track to the play queue.
  /// @param track Track number (1-9999).
  /// @param volume Volume level (0-30).
  /// @param red Red LED color component.
  /// @param green Green LED color component.
  /// @param blue Blue LED color component.
  void play(uint16_t track, uint8_t volume, uint8_t red, uint8_t green, uint8_t blue);

  /// @brief Initializes the DFPlayer (currently unused).
  /// @return `true`.
  bool init() override { return true; }

  /// @brief Manages the state machine for track playback.
  /// Should be called periodically in the main loop.
  /// @return `true`.
  bool run() override;

  /// @brief Prints errors from the DFPlayer module.
  using DFPlayerMiniFast::printError;

  DFPlayer(const DFPlayer&) = delete;                       // Define copy constructor.
  DFPlayer& operator=(const DFPlayer&) = delete;            // Define copy assignment operator.
  DFPlayer(DFPlayer&&) = delete;                            // Define move constructor.
  DFPlayer& operator=(DFPlayer&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Attaches an interrupt to the specified pin.
  void attachInt() const {
    // Clear interrupt flag, because it stores the interrupt event, even it is not attached.
    bitSet(EIFR, digitalPinToInterrupt(intPin));
    attachInterrupt(digitalPinToInterrupt(intPin), irqHandler, RISING);
  }

  /// @brief Detaches the interrupt from the specified pin.
  inline void detachInt() const {
    detachInterrupt(digitalPinToInterrupt(intPin));
  }

  /// @brief Interrupt handler to set the play flag.
  static inline void irqHandler() { enablePlay = true; }

  /// @brief State machine states for playing.
  enum class PlayingStates : uint8_t {
    IDLE,                           // Check the queue for tracks to play.
    TURN_ON,                        // Turn on the DFPlayer module.
    WAIT_FOR_BOOT,                  // Wait for the DFPlayer to boot.
    SET_VOLUME,                     // Set the playback volume.
    WAIT_FOR_CMD,                   // Wait for the command to complete.
    PLAY,                           // Start playing the current track.
    WAIT_FOR_PLAY,                  // Wait for the track to finish.
    CHECK_QUEUE,                    // Check if more tracks are in the queue.
    PLAYING_DELAY,                  // Delay between consecutive tracks.
    TURN_OFF,                       // Turn off the DFPlayer module.
  };

  static constexpr uint16_t bootTime = Time::secToMs(1U);                   // DFPlayer boot time (ms).
  static constexpr uint8_t cmdExecTime = 120U;                              // Command execution time (ms).
  static constexpr uint16_t playDelayTime = 400U;                           // Delay between tracks (ms).
  static constexpr uint16_t playTimeoutTime = Time::secToMs(10U);           // Timeout for track playback (ms).

  static volatile bool enablePlay;                                          // Interrupt flag indicating readiness to play.
  RgbLedWrapper& rgbLed;                                                    // Reference to the RGB LED controller.
  SoftwareSerial swSerial;                                                  // Software serial object.
  const uint8_t rxPin;                                                      // RX pin for software serial.
  const uint8_t txPin;                                                      // TX pin for software serial.
  const uint8_t enPin;                                                      // Power control pin.
  const uint8_t intPin;                                                     // Interrupt pin (LOW when playing).
  uint32_t eventTimer;                                                      // Class wide variable for universal timings.
  PlayingStates playingState;                                               // Current state of the playback state machine.
  CircularBuffer<PlayQueueItem, 5U> playingQueue;                           // Playback queue for tracks.
};
#endif // DFPLAYER_HPP
