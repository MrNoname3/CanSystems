#ifndef CAN_COMMANDS_HPP
#define CAN_COMMANDS_HPP

#include <stdint.h>

/// @brief Base command list for nodes.
enum class CanCmd : uint16_t {
  PING = 0,                                   // Ping command.
  RESTART,                                    // Node reset command.
  FW_VERSION,                                 // Get node firmware version.
  BUTTON_EVENT,                               // Get button event type.
  OTA_START,                                  // Init FW file transfer.
  OTA_SEND,                                   // Send FW file pieces.
  OTA_END,                                    // FW transfer process ended.

  RGB_LED = 32,                               // Set WS2812 RGB LED color.
  PLAY_MP3,                                   // Play MP3 file.
  READ_HUM_TEMP_LDR,                          // Read humidity, temperature and light value.
};
#endif