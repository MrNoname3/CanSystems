#ifndef MAIN_HPP
#define MAIN_HPP

//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.
#include <CAN.h>                              /// SPI CAN controller library.
#include <EEPROM.h>                           /// EEPROM access library.
#include <avr/wdt.h>                          /// Watchdog timer library.
#include <NeoPixelBus.h>                      /// WS2812 LED driver library.
#include <PushButtonClicks.h>                 /// Pushbutton events library.
#include "CircularBuffer.hpp"                 /// Circular buffer class.
#include "DFPlayer.hpp"                       /// MP3 player driver library.
#include <SI7021.h>                           /// Temperature and humidity sensor driver.

//--- Constants ---//
static constexpr const char* SW_VERSION             = "V1.0.0";     // Actual software version.
static constexpr const char* OK_STATE               = " [ OK ]";    // OK status.
static constexpr const char* ERR_STATE              = " [ ERR ]";   // Error status.
static constexpr const char* SAVED_STATE            = "[S] ";       // Saved data mark.
static constexpr const char* DEFAULT_STATE          = "[D] ";       // Default data mark.
static constexpr uint8_t EEPROM_VALID               = 231;          // EEPROM validity check.
static constexpr uint16_t DEFAULT_LOCAL_ADDRESS     = 444;          // Node default address if no saved available.
static constexpr uint16_t DEFAULT_MASTER_ADDRESS    = 10;           // Default CAN master address.
static constexpr uint32_t CAN_MASK                  = 0x1FF80000;   // CAN extended ID mask.
static constexpr uint8_t RGB_LED_NUM                = 19;           // Number of LED's.
  
static constexpr uint8_t RGB_PIN                    = 7;            // LED DATA PIN
static constexpr uint8_t LED                        = 4;            // Pin of the LED.
static constexpr uint8_t CAN_INT                    = 2;            // Interrupt pin of the SPI CAN controller.
static constexpr uint8_t BUTTON                     = 8;            // Pushbutton pin.
static constexpr uint8_t DFP_EN                     = 9;            // DFPlayer switch pin.
static constexpr uint8_t DFP_BUSY                   = 3;            // DFPlayer busy pin.
static constexpr uint8_t DFP_TX                     = 5;            // DFPlayer serial RX pin.
static constexpr uint8_t DFP_RX                     = 6;            // DFPlayer serial TX pin.
static constexpr uint8_t CHARGE_PIN                 = 17;           // Capacitor charge enable pin. (A3)
static constexpr uint8_t RF_RX                      = 16;           // RF serial RX pin. (A2)
static constexpr uint8_t RF_TX                      = 15;           // RF serial TX pin. (A1)

#define LED_T (PORTD ^=  (1 << PORTD4))       // LED pin toggle.
#define LED_H (PORTD |=  (1 << PORTD4))       // LED pin high.
#define LED_L (PORTD &= ~(1 << PORTD4))       // LED pin low.
#define NOP   __asm__("nop\n\t");             // 1 CPU cycle delay.

//--- Structs ---//

struct Settings {                             // The struct of the settings in the EEPROM.
  uint8_t isValid = 0;                        // Variable of address data validity.
  uint16_t canAddress = DEFAULT_LOCAL_ADDRESS;  // Variable of CAN address.
};

/// @brief Color values for RGB LED.
struct RGBValues {
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
};

//--- Enums ---//

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
  ECMD_CHARGE_DISPLAY,                        // Enable/disable external sensor charging.
  ECMD_READ_HUMTEMP,                          // Read humidity and temperature.

  ECMD_LAST_ELEMENT                           // Last element of enum!
};

/// @brief Callback types.
enum class canCb : uint8_t {
  NODE_CB_IDLE = 0,                           // No event state.
  NODE_CB_RESTARTED,                          // Node restarted.

  NODE_CB_LAST_ELEMENT                        // Last element of enum!
};

/// @brief Error types.
enum class errorTypes : uint8_t {
  ERR_I2C_READ_TIMEOUT = 0,                   // I2C read timeout.
  ERR_I2C_SENSOR_INIT,                        // I2C sensor init failed.
  ERR_CAN_INIT,                               // CAN init failed.
  ERR_CAN_ID_SET,                             // CAN ID setup method failed.
  ERR_CAN_DATA_WRITE,                         // CAN data write method failed.
  ERR_CAN_ENDPACKET,                          // CAN endpacket method failed.
  ERR_UNHANDLED_COMMAND,                      // Unhandled command in state machine.

  LAST_ELEMENT                                // Last element of enum!
};

/// @brief States of SI7021 reads.
enum class si7021States : uint8_t {
  IDLE = 0,
  READ_TEMPERATURE,
  READ_HUMIDITY,

  LAST_ELEMENT
};

//--- Functions ---//

/// @brief Handles the I2C humidity and temperature sensor.
inline void handleHumTempSensor() __attribute__((always_inline));

/// @brief Turns on/off the external
/// temperature and humidity sensor with LCD screen.
inline void handleExtSensors() __attribute__((always_inline));

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
void resetCMD();

/// @brief Load saved data from EEPROM to settings structure.
void LoadFromEEPROM();

/// @brief Save data to EEPROM from the settings structure.
bool SaveToEEPROM();

/// @brief Converts the given data to extended CAN ID.
/// @param from Sender address.
/// @param cmd Command from sender.
/// @param to_ Target address.
/// @return Returns with the CAN address.
uint32_t dataToExtId(uint16_t from, uint16_t cmd, uint16_t to_);

/// @brief Converts the given extended CAN ID to addresses and command variable.
/// @param extId Extended CAN ID.
/// @param from Pointer to sender address variable.
/// @param cmd Pointer to command variable.
/// @param to_ Pointer to target address variable.
void extIdToData(uint32_t extId, uint16_t* from, uint16_t* cmd, uint16_t* to_);

/// @brief Handles the interrupts from the SPI CAN controller.
void canIrqHandler();

/// @brief Reset the microcontroller.
void (*resetFunc)() = nullptr;


#endif // MAIN_HPP
