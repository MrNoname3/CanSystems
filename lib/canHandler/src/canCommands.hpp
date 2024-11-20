#ifndef CAN_COMMANDS_HPP
#define CAN_COMMANDS_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

static constexpr uint16_t CBS = 32U;          // Command block separator.

/// @brief Enumeration of CAN commands for communication between nodes.
enum class CanCmd : uint16_t {
  PING = 0U,                                  // Ping command to check node availability.
  RESTART,                                    // Command to reset/restart the node.
  FW_VERSION,                                 // Request the firmware version from the node.
  BUTTON_EVENT,                               // Retrieve button event type from the node.
  OTA_START,                                  // Initialize a firmware over-the-air (OTA) update.
  OTA_SEND,                                   // Send firmware data chunks for OTA updates.
  OTA_END,                                    // Signal the end of the OTA update process.
  RGB_LED,                                    // Set the color of WS2812 RGB LEDs.

  PLAY_MP3 = CBS,                             // Command to play an MP3 file on the node.
  READ_HUM_TEMP_LDR,                          // Read humidity, temperature, and light sensor values.

  ADD_IRRIGATION = CBS,                       // Add a new irrigation task to the queue.
  SKIP_IRRIGATION,                            // Skip the currently scheduled irrigation task.
  STOP_IRRIGATION,                            // Stop all ongoing and scheduled irrigation tasks.
  IRRIGATION_ERROR,                           // Report an error during irrigation.
  MOISTURE_DATA,                              // Provide moisture level data from sensors.
};
#endif