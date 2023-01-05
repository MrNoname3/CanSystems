#ifndef __MAIN_HPP__
#define __MAIN_HPP__

#include <Arduino.h>			                  /// Arduino libraries header.
#include <CAN.h>				                    /// SPI CAN controller library.
#include <EEPROM.h>				                  /// EEPROM access library.
#include <avr/wdt.h>			                  /// Watchdog timer library.
#include <FastLED.h>                        /// WS2812 LED driver library.
#include <PushButtonClicks.h>               /// Pushbutton events library.
#include "CircularBuffer.hpp"               /// Circular buffer class.
#include "MillisTimer.hpp"                  /// Millisec timer class.

////////////////////////////////////////////////////
//#include "DFPlayerMiniFast.h"
//#include <SoftwareSerial.h>
#include "DFPlayer.hpp"
////////////////////////////////////////////////////

#define RGB_LED_NUM   1                       // 1pcs LED.
#define CHIP_SET      WS2812B                 // Types of RGB LEDs.
#define COLOR_CODE    GRB                     // Sequence of colors in data stream.

#define RGB_PIN   7                           // LED DATA PIN
#define LED       4				                    // Pin of the LED.
#define CAN_INT   2						                // Interrupt pin of the SPI CAN controller.
#define BUTTON    8                           // Pushbutton pin.
#define DFP_EN    9                           // DFPlayer switch pin.
#define DFP_BUSY  3                           // DFPlayer busy pin.
#define DFP_TX    5                           // DFPlayer serial RX pin.
#define DFP_RX    6                           // DFPlayer serial TX pin.

#define LED_T (PORTD ^=  (1 << PORTD4))       // LED pin toggle.
#define LED_H (PORTD |=  (1 << PORTD4))       // LED pin high.
#define LED_L (PORTD &= ~(1 << PORTD4))       // LED pin low.
#define NOP   __asm__("nop\n\t");			        // 1 CPU cycle delay.

struct Settings {					                    // The struct of the settings in the EEPROM.
  uint8_t isValid;				                    // Variable of address data validity.
  uint16_t canAddress;				                // Variable of CAN address.
};

void (*resetFunc)(void) = 0;   	              // Declare reset function at address 0.

/// @brief General command list for nodes.
enum class canCmd : uint8_t {
  NODE_CMD_IDLE = 0,                          // Idle state.
  NODE_CMD_PING,                              // Ping command.
  NODE_CMD_RESET,	                            // Node reset command.
  NODE_CMD_GET_FW_VERSION,                    // Firmware version command.
  NODE_CMD_SETADDRESS,                        // CAN address setup.
  NODE_CMD_RGB_LED,                           // Set WS2812 RGB LED color.
  NODE_CMD_GET_BUTTON_EVENT,		              // Get button event type.
	NODE_CMD_MOISTURE,           	              // Get moisture data.
	NODE_CMD_LDR,           	                  // Get ldr data.
	NODE_CMD_IRRIGATION,                        // Plant irrigation.

  NODE_CMD_LAST_ELEMENT                       // Last element of enum!
};

/// @brief Callback types.
enum class canCb : uint8_t {
  NODE_CB_IDLE = 0,                           // No event state.
  NODE_CB_RESTARTED,                          // Node restarted.
  NODE_CB_ERROR,	    					              // Error type callback.
  NODE_CB_BUTTON_EVENT,                       // Button pressed on node callback.

  NODE_CB_LAST_ELEMENT                        // Last element of enum!
};

/// MCU reset
///
/// @brief This command resets the MCU.
void resetCMD(void);

/// Analog readings.
///
/// @brief This function reads the analog sensors in the loop.
void AnalogReads(void);

/// Load data.
///
/// @brief Loads saved data from EEPROM to the above mentioned structure.
void LoadFromEEPROM(void);

/// Save data.
///
/// @brief Saves data to EEPROM from the above mentioned structure.
bool SaveToEEPROM(void);

/// Extended ID converter.
///
/// @brief Converts the given data to extended CAN ID.
/// @param from Sender address.
/// @param cmd Command from sender.
/// @param to Target address.
/// @return Returns with the CAN address.
uint32_t dataToExtId(uint16_t from, uint16_t cmd, uint16_t to);

/// Data converter.
///
/// @brief Converts the given extended CAN ID to addresses and command variable.
/// @param extId Extended CAN ID.
/// @param from Pointer to sender address variable.
/// @param cmd Pointer to command variable.
/// @param to Pointer to target address variable.
void extIdToData(uint32_t extId, uint16_t* from, uint16_t* cmd, uint16_t* to);

/// Interrupt handler.
///
/// @brief Handles the interrupts from the SPI CAN controller.
void canIrqHandler(void);


#endif