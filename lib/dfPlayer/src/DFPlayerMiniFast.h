#ifndef DFPLAYERMINIFAST_H
#define DFPLAYERMINIFAST_H

#include <Arduino.h>                                                /// Arduino framework header.
#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Driver for the YX5200-24SS MP3 player (DFPlayerMini) over UART.
/// @note Communicates via a 10-byte packet protocol: start byte, version, length,
///       command, feedback flag, MSB param, LSB param, checksum MSB, checksum LSB, end byte.
/// @note The checksum is the two's complement negation of the sum of bytes 2–7 (version through LSB param).
class DFPlayerMiniFast {

public:

  enum class PacketValues : uint8_t {
    STACK_SIZE      = 10U,          // Total number of bytes in a packet (same for cmds and queries).
    SB              = 0x7EU,        // Start byte.
    VER             = 0xFFU,        // Version.
    LEN             = 0x6U,         // Number of bytes after LEN (excluding checksum and end byte).
    FEEDBACK        = 1U,           // Feedback requested.
    NO_FEEDBACK     = 0U,           // No feedback requested.
    EB              = 0xEFU         // End byte.
  };

  enum class ControlCommandValues : uint8_t {
    NEXT            = 0x01U,
    PREV            = 0x02U,
    PLAY            = 0x03U,
    INC_VOL         = 0x04U,
    DEC_VOL         = 0x05U,
    VOLUME          = 0x06U,
    EQ              = 0x07U,
    PLAYBACK_MODE   = 0x08U,
    PLAYBACK_SRC    = 0x09U,
    STANDBY         = 0x0AU,
    NORMAL          = 0x0BU,
    RESET           = 0x0CU,
    PLAYBACK        = 0x0DU,
    PAUSE           = 0x0EU,
    SPEC_FOLDER     = 0x0FU,
    VOL_ADJ         = 0x10U,
    REPEAT_PLAY     = 0x11U,
    USE_MP3_FOLDER  = 0x12U,
    INSERT_ADVERT   = 0x13U,
    SPEC_TRACK_3000 = 0x14U,
    STOP_ADVERT     = 0x15U,
    STOP            = 0x16U,
    REPEAT_FOLDER   = 0x17U,
    RANDOM_ALL      = 0x18U,
    REPEAT_CURRENT  = 0x19U,
    SET_DAC         = 0x1AU
  };

  enum class QueryCommandValues : uint8_t {
    SEND_INIT        = 0x3FU,
    RETRANSMIT       = 0x40U,
    REPLY            = 0x41U,
    GET_STATUS_      = 0x42U,
    GET_VOL          = 0x43U,
    GET_EQ           = 0x44U,
    GET_MODE         = 0x45U,
    GET_VERSION      = 0x46U,
    GET_TF_FILES     = 0x47U,
    GET_U_FILES      = 0x48U,
    GET_FLASH_FILES  = 0x49U,
    KEEP_ON          = 0x4AU,
    GET_TF_TRACK     = 0x4BU,
    GET_U_TRACK      = 0x4CU,
    GET_FLASH_TRACK  = 0x4DU,
    GET_FOLDER_FILES = 0x4EU,
    GET_FOLDERS      = 0x4FU
  };

  enum class EqValues : uint8_t {
    EQ_NORMAL       = 0U,
    EQ_POP          = 1U,
    EQ_ROCK         = 2U,
    EQ_JAZZ         = 3U,
    EQ_CLASSIC      = 4U,
    EQ_BASE         = 5U
  };

  enum class ModeValues : uint8_t {
    REPEAT          = 0U,
    FOLDER_REPEAT   = 1U,
    SINGLE_REPEAT   = 2U,
    RANDOM          = 3U
  };

  enum class PlaybackSourceValues : uint8_t {
    U               = 1U,
    TF              = 2U,
    AUX             = 3U,
    SLEEP           = 4U,
    FLASH           = 5U
  };

  enum class BaseVolumeAdjustValue : uint8_t {
    VOL_ADJUST      = 0x10U
  };

  enum class RepeatPlayValues : uint8_t {
    STOP_REPEAT     = 0U,
    START_REPEAT    = 1U
  };

  /// @brief Configure the class.
  /// @param stream A reference to the Serial instance (hardware or software) used to communicate with the MP3 player.
  /// @param debug Set to `true` to enable debug prints to the serial monitor.
  /// @param threshold Number of ms allowed for the MP3 player to respond (timeout) to a query.
  /// @return `true`.
  bool begin(Stream& stream, bool debug = false, uint16_t threshold = 10U);

  /// @brief Play the next song in chronological order.
  void playNext();

  /// @brief Play the previous song in chronological order.
  void playPrevious();

  /// @brief Play a specific track.
  /// @param trackNum The track number to play.
  void play(uint16_t trackNum);

  /// @brief Stop the current playback.
  void stop();

  /// @brief Play a specific track in the folder named "MP3".
  /// @param trackNum The track number to play.
  void playFromMP3Folder(uint16_t trackNum);

  /// @brief Interrupt the current track with a new track.
  /// @param trackNum The track number to play.
  void playAdvertisement(uint16_t trackNum);

  /// @brief Stop the interrupting track.
  void stopAdvertisement();

  /// @brief Increment the volume by 1 out of 30.
  void incVolume();

  /// @brief Decrement the volume by 1 out of 30.
  void decVolume();

  /// @brief Set the volume to a specific value out of 30.
  /// @param level The volume level (0–30).
  void volume(uint8_t level);

  /// @brief Set the EQ mode.
  /// @param setting The desired EQ ID.
  void EQSelect(uint8_t setting);

  /// @brief Loop a specific track.
  /// @param trackNum The track number to play.
  void loop(uint16_t trackNum);

  /// @brief Specify the playback source.
  /// @param source The playback source ID.
  void playbackSource(uint8_t source);

  /// @brief Put the MP3 player in standby mode (this is NOT sleep mode).
  void standbyMode();

  /// @brief Pull the MP3 player out of standby mode.
  void normalMode();

  /// @brief Reset all settings to factory default.
  void reset();

  /// @brief Resume playing current track.
  void resume();

  /// @brief Pause playing current track.
  void pause();

  /// @brief Play a specific track from a specific folder.
  /// @param folderNum The folder number.
  /// @param trackNum The track number to play.
  void playFolder(uint8_t folderNum, uint8_t trackNum);

  /// @brief Play a specific track from a specific folder, where track names are 4-digit numbered
  ///        (e.g. 1234-mysong.mp3) and can be up to 3000. Only folders "01" to "15" are supported.
  /// @param folderNum The folder number.
  /// @param trackNum The track number to play.
  void playLargeFolder(uint8_t folderNum, uint16_t trackNum);

  /// @brief Specify volume gain.
  /// @param gain The specified volume gain (0–31).
  void volumeAdjustSet(uint8_t gain);

  /// @brief Play all tracks.
  void startRepeatPlay();

  /// @brief Stop repeat play.
  void stopRepeatPlay();

  /// @brief Play all tracks in a given folder.
  /// @param folder The folder number.
  void repeatFolder(uint16_t folder);

  /// @brief Play all tracks in a random order.
  void randomAll();

  /// @brief Repeat the current track.
  void startRepeat();

  /// @brief Stop repeat play of the current track.
  void stopRepeat();

  /// @brief Turn on DAC.
  void startDAC();

  /// @brief Turn off DAC.
  void stopDAC();

  /// @brief Put the MP3 player into sleep mode.
  void sleep();

  /// @brief Pull the MP3 player out of sleep mode.
  void wakeUp();

  /// @brief Determine if a track is currently playing.
  /// @return `true` if a track is playing, `false` if not or on error.
  [[nodiscard]] bool isPlaying();

  /// @brief Determine the current volume setting.
  /// @return Volume level (0–30), or -1 on error.
  [[nodiscard]] int16_t currentVolume();

  /// @brief Determine the current EQ setting.
  /// @return EQ setting, or -1 on error.
  [[nodiscard]] int16_t currentEQ();

  /// @brief Determine the current mode.
  /// @return Mode, or -1 on error.
  [[nodiscard]] int16_t currentMode();

  /// @brief Determine the current firmware version.
  /// @return Firmware version, or -1 on error.
  [[nodiscard]] int16_t currentVersion();

  /// @brief Determine the number of tracks accessible via USB.
  /// @return Number of tracks, or -1 on error.
  [[nodiscard]] int16_t numUsbTracks();

  /// @brief Determine the number of tracks accessible via SD card.
  /// @return Number of tracks, or -1 on error.
  [[nodiscard]] int16_t numSdTracks();

  /// @brief Determine the number of tracks accessible via flash.
  /// @return Number of tracks, or -1 on error.
  [[nodiscard]] int16_t numFlashTracks();

  /// @brief Determine the current track played via USB.
  /// @return Current track number, or -1 on error.
  [[nodiscard]] int16_t currentUsbTrack();

  /// @brief Determine the current track played via SD card.
  /// @return Current track number, or -1 on error.
  [[nodiscard]] int16_t currentSdTrack();

  /// @brief Determine the current track played via flash.
  /// @return Current track number, or -1 on error.
  [[nodiscard]] int16_t currentFlashTrack();

  /// @brief Determine the number of tracks in the specified folder.
  /// @param folder The folder number.
  /// @return Number of tracks, or -1 on error.
  [[nodiscard]] int16_t numTracksInFolder(uint8_t folder);

  /// @brief Determine the number of folders available.
  /// @return Number of folders, or -1 on error.
  [[nodiscard]] int16_t numFolders();

  /// @brief Set the timeout value for MP3 player query responses.
  /// @param threshold Number of ms allowed for the MP3 player to respond.
  void setTimeout(uint16_t threshold);

  /// @brief Print the error description if an error packet has been received.
  void printError() const;

  DFPlayerMiniFast() = default;
  ~DFPlayerMiniFast() = default;
  DFPlayerMiniFast(const DFPlayerMiniFast&) = delete;
  DFPlayerMiniFast& operator=(const DFPlayerMiniFast&) = delete;
  DFPlayerMiniFast(DFPlayerMiniFast&&) = delete;
  DFPlayerMiniFast& operator=(DFPlayerMiniFast&&) = delete;

private:

  /// @brief Result returned by processState() for each received byte.
  enum class ParseResult : uint8_t { CONTINUE, SUCCESS, FAILURE };

  /// @brief MP3 response packet parsing states.
  enum class Fsm : uint8_t {
    find_start_byte,
    find_ver_byte,
    find_len_byte,
    find_command_byte,
    find_feedback_byte,
    find_param_MSB,
    find_param_LSB,
    find_checksum_MSB,
    find_checksum_LSB,
    find_end_byte
  };

  /// @brief Serial data packet used for MP3 config/control.
  struct Stack {
    uint8_t start_byte;
    uint8_t version;
    uint8_t length;
    uint8_t commandValue;
    uint8_t feedbackValue;
    uint8_t paramMSB;
    uint8_t paramLSB;
    uint8_t checksumMSB;
    uint8_t checksumLSB;
    uint8_t end_byte;
  };

  /// @brief Calculate the two's-complement checksum and write it into the packet struct.
  /// @param _stack Reference to the packet to checksum.
  static void findChecksum(Stack& _stack);

  /// @brief Send the current sendStack packet over serial.
  void sendData();

  /// @brief Discard all bytes currently in the serial receive buffer.
  void flush();

  /// @brief Send a query command and wait for the MP3 player's response.
  /// @param cmd The query command byte.
  /// @param msb Parameter MSB (default 0).
  /// @param lsb Parameter LSB (default 0).
  /// @return 16-bit response value, or -1 on timeout or error.
  [[nodiscard]] int16_t query(uint8_t cmd, uint8_t msb = 0U, uint8_t lsb = 0U);

  /// @brief Print the full contents of a packet struct for debugging.
  /// @param _stack The packet to print.
  void printStack(Stack _stack);

  /// @brief Advance the FSM by one received byte.
  /// @param recChar The byte received from the MP3 player.
  /// @return `ParseResult::SUCCESS` on complete packet, `FAILURE` on protocol error, `CONTINUE` otherwise.
  [[nodiscard]] ParseResult processState(uint8_t recChar);

  /// @brief Verify the received checksum against the calculated one; resets state on mismatch.
  /// @return `true` if checksum matches, `false` otherwise.
  [[nodiscard]] bool verifyChecksum();

  /// @brief Print a flash-stored message to Serial if debug mode is enabled.
  /// @param msg Message stored in program memory.
  void debugLog(const __FlashStringHelper* msg) const;

  /// @brief Wait for and parse the MP3 player's serial response.
  /// @return `true` on successful packet reception, `false` on error or timeout.
  [[nodiscard]] bool parseFeedback();

  Stream* serial = nullptr;                                         // Serial port connected to the MP3 player.
  bool debug = false;                                               // Enable debug prints when true.
  uint32_t timeoutTimer = 0UL;                                      // Timestamp of the last query send.
  uint16_t timeoutTime = 10U;                                       // Max ms to wait for a query response.
  Fsm state = Fsm::find_start_byte;                                 // Current state of the response parser.
  Stack sendStack = {};                                             // Outgoing packet buffer.
  Stack recStack  = {};                                             // Incoming packet buffer.
};

#endif // DFPLAYERMINIFAST_H
