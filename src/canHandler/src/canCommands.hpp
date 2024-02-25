#ifndef CAN_COMMANDS_HPP
#define CAN_COMMANDS_HPP

#include <stdint.h>

/// @brief Base command list for nodes.
enum class CanCmd : uint16_t {
  PING = 0,                                   // Ping command.
  RESTART,                                    // Node reset command.
  BUTTON_EVENT,                               // Get button event type.
  FILET_START,                                // Init file transfer process.
  FILET_SEND,                                 // Send file pieces.
  FILET_END,                                  // File transfer process ended.

  RGB_LED = 32,                               // Set WS2812 RGB LED color.
  PLAY_MP3,                                   // Play MP3 file.
  READ_HUM_TEMP_LDR,                          // Read humidity, temperature and light value.
};
#endif