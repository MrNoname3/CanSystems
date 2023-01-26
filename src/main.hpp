#ifndef __MAIN_HPP__
#define __MAIN_HPP__

#include <Arduino.h>                        /// Arduino libraries header.
#include <CAN.h>                            /// SPI CAN controller library.
#include <EEPROM.h>	                        /// EEPROM access library.
#include <avr/wdt.h>                        /// Watchdog timer library.
#include <FastLED.h>                        /// WS2812 LED driver library.
#include <PushButtonClicks.h>               /// Pushbutton events library.
#include "CircularBuffer.hpp"               /// Circular buffer class.
#include "MillisTimer.hpp"                  /// Millisec timer class.
#include "DFPlayer.hpp"                     /// MP3 player driver library.

#define RGB_LED_NUM   1                       // 1pcs LED.
#define CHIP_SET      WS2812B                 // Types of RGB LEDs.
#define COLOR_CODE    GRB                     // Sequence of colors in data stream.

#define RGB_PIN   7                           // LED DATA PIN
#define LED       4                           // Pin of the LED.
#define CAN_INT   2                           // Interrupt pin of the SPI CAN controller.
#define BUTTON    8                           // Pushbutton pin.
#define DFP_EN    9                           // DFPlayer switch pin.
#define DFP_BUSY  3                           // DFPlayer busy pin.
#define DFP_TX    5                           // DFPlayer serial RX pin.
#define DFP_RX    6                           // DFPlayer serial TX pin.

#define LED_T (PORTD ^=  (1 << PORTD4))       // LED pin toggle.
#define LED_H (PORTD |=  (1 << PORTD4))       // LED pin high.
#define LED_L (PORTD &= ~(1 << PORTD4))       // LED pin low.
#define NOP   __asm__("nop\n\t");             // 1 CPU cycle delay.

struct Settings {                             // The struct of the settings in the EEPROM.
  uint8_t isValid;                            // Variable of address data validity.
  uint16_t canAddress;                        // Variable of CAN address.
};

void (*resetFunc)(void) = 0;                  // Declare reset function at address 0.

/// @brief Base command list for nodes.
enum class canCmdB : uint16_t {
  BCMD_IDLE = 0,                              // Idle state.
  BCMD_PING,                                  // Ping command.
  BCMD_RESET,                                 // Node reset command.
  BCMD_GET_FW_VERSION,                        // Firmware version command.
  BCMD_SETADDRESS,                            // CAN address setup.
  BCMD_RGB_LED,                               // Set WS2812 RGB LED color.
  BCMD_GET_BUTTON_EVENT,                      // Get button event type.

  BCMD_LAST_ELEMENT                           // Last element of enum!
};

/// @brief Node type specific extended command list.
enum class canCmdE : uint16_t {
  ECMD_PLAY_MP3 = static_cast<uint16_t>(canCmdB::BCMD_LAST_ELEMENT),   // Play MP3 file.

  ECMD_LAST_ELEMENT                           // Last element of enum!
};

/// @brief Callback types.
enum class canCb : uint16_t {
  NODE_CB_IDLE = 0,                           // No event state.
  NODE_CB_RESTARTED,                          // Node restarted.
  NODE_CB_ERROR,                              // Error type callback.
  //NODE_CB_BUTTON_EVENT,                       // Button pressed on node callback.

  NODE_CB_LAST_ELEMENT                        // Last element of enum!
};

/// @brief Color values for RGB LED.
struct RGBValues {
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
};

/// @brief Put the given data to RGB LED queue.
/// @param red Value of red color: 0-255.
/// @param green Value of green color: 0-255.
/// @param blue Value of blue color: 0-255.
void addToRGBQueue(const uint8_t red, const uint8_t green, const uint8_t blue);

/// @brief Disable answer to standard CAN IDs,
/// because it is a broadcast message for all nodes.
/// @param extId Extended CAN ID.
/// @param data Data array to be send.
/// @param size Size of data array. Maximum 8 byte.
void sendCanResponse(const uint32_t extId, const uint8_t data[], const uint8_t size);

/// @brief Reset the MCU.
void resetCMD(void);

/// @brief Load saved data from EEPROM to settings structure.
void LoadFromEEPROM(void);

/// @brief Save data to EEPROM from the settings structure.
bool SaveToEEPROM(void);

/// @brief Converts the given data to extended CAN ID.
/// @param from Sender address.
/// @param cmd Command from sender.
/// @param to Target address.
/// @return Returns with the CAN address.
uint32_t dataToExtId(uint16_t from, uint16_t cmd, uint16_t to);

/// @brief Converts the given extended CAN ID to addresses and command variable.
/// @param extId Extended CAN ID.
/// @param from Pointer to sender address variable.
/// @param cmd Pointer to command variable.
/// @param to Pointer to target address variable.
void extIdToData(uint32_t extId, uint16_t* from, uint16_t* cmd, uint16_t* to);

/// @brief Handles the interrupts from the SPI CAN controller.
void canIrqHandler(void);


#endif