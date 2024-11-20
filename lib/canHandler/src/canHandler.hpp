#ifndef CAN_HANDLER_HPP
#define CAN_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <HardwareSerial.h>                                         /// Serial driver object.
#include "canCommands.hpp"                                          /// Definitions of CAN commands.
#include "eepromHandler.hpp"                                        /// EEPROM wrapper class.
#include <SPIFlash.h>                                               /// SPI FLASH module driver.
#include "ota.hpp"                                                  /// OTA (Over-The-Air) update handler.
#include "taskRunner.hpp"                                           /// Base class for task scheduling.

/// @brief Handles CAN communication, and OTA updates.
/// @details The `CanHandler` class manages the CAN communication protocol
/// for interacting with devices. It also facilitates Over-The-Air updates.
class CanHandler final : public TaskRunner {
private:
#ifdef NEW_CAN_ADDRESS
  #warning "NEW_CAN_ADDRESS is defined!"
  static_assert(NEW_CAN_ADDRESS < 1023, "New CAN address must be less than 1023!");
  static constexpr uint16_t newCanAddress = static_cast<uint16_t>(NEW_CAN_ADDRESS);
#endif
  static_assert(MASTER_CAN_ADDRESS < 1023, "Master CAN address must be less than 1023!");
  static constexpr uint16_t masterCanId = static_cast<uint16_t>(MASTER_CAN_ADDRESS);
public:
  static constexpr const char* OK_STATE               = "[OK]";   // Status indicating success.
  static constexpr const char* ERR_STATE              = "[ERR]";  // Status indicating failure.

  /// @brief Structure representing a CAN frame.
  /// @details Includes an extended CAN ID with 3 parts: receiver address (`to`), command (`cmd`), and sender address (`from`).
  /// Also includes a data payload that can hold up to 8 bytes.
  struct __attribute__((packed))
  CanFrame {
    union {
      uint32_t extId;                           // Extended CAN ID.
      struct {
        uint32_t to : 10;                       // 10 bits for the receiver address.
        uint32_t cmd : 9;                       // 9 bits for the command.
        uint32_t from : 10;                     // 10 bits for the sender address.
        uint32_t padding : 3;                   // Padding to fill up to 32 bits.
      };
    };
    uint8_t data[8];                            // Data payload (up to 8 bytes).
    CanFrame() : extId(), data{0} {}            // Default constructor initializing `extId` and `data`.
  };

  /// @brief Enum representing response statuses.
  enum class Response : uint8_t {
    NACK = 0,                                   // Negative acknowledgment.
    ACK                                         // Acknowledgment.
  };

  /// @brief Constructor for the CAN handler.
  /// @param serial Reference to a `HardwareSerial` object.
  /// @param canCsPin Chip select pin for the CAN module.
  /// @param canIntPin Interrupt pin for the CAN module.
  /// @param ledPin LED control pin.
  /// @param flashCsPin Chip select pin for the SPI flash.
  CanHandler(HardwareSerial& serial, uint8_t canCsPin, uint8_t canIntPin, uint8_t ledPin, uint8_t flashCsPin);

  /// @brief Default destructor.
  ~CanHandler() = default;

  /// @brief Begins CAN communication with the specified baud rate.
  /// @param canBaud Baud rate for CAN communication.
  void begin(uint32_t canBaud);

  /// @brief Initializes the CAN handler.
  /// @details Sets the CAN bus speed to 500 Kb/s.
  virtual void init() override { begin(500000U); }

  /// @brief Executes the main loop for CAN communication.
  virtual void run() override;

  /// @brief Sends a CAN frame with a specified command and data payload.
  /// @param command 16-bit command value.
  /// @param data Array of 8 bytes containing the payload.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  bool send(uint16_t command, const uint8_t (&data)[8]) const;

  /// @brief Sends a CAN frame with a specified command.
  /// @param command 16-bit command value.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  bool send(uint16_t command) const;

  /// @brief Sends a CAN frame using an enumeration command and data payload.
  /// @param command Enum value of `CanCmd`.
  /// @param data Array of 8 bytes containing the payload.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  bool send(CanCmd command, const uint8_t (&data)[8]) const;

  /// @brief Sends a CAN frame using an enumeration command.
  /// @param command Enum value of `CanCmd`.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  bool send(CanCmd command) const;

  /// @brief Sends a CAN frame with a command and a response.
  /// @param command Enum value of `CanCmd`.
  /// @param response Enum value of `Response`.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  bool send(CanCmd command, Response response) const;

  /// @brief Turns on the LED (set to HIGH).
  inline void ledOn() const { digitalWrite(ledPin, HIGH); }

  /// @brief Turns off the LED (set to LOW).
  inline void ledOff() const { digitalWrite(ledPin, LOW); }

  /// @brief Toggles the LED state (HIGH to LOW, or LOW to HIGH).
  inline void ledToggle() const { digitalWrite(ledPin, !digitalRead(ledPin)); }

  /// @brief Resets the microcontroller unit (MCU) by triggering a watchdog reset.
  void restartMCU() const;

  /// @brief Adds a custom callback for handling incoming CAN frames.
  /// @param canCallback Pointer to the callback function.
  inline void addCanCallback(void (*canCallback)(uint16_t command, const uint8_t (&data)[8])) {
    this->canCallback = canCallback;
  }

  CanHandler(const CanHandler&) = delete;                       // Define copy constructor.
  CanHandler& operator=(const CanHandler&) = delete;            // Define copy assignment operator.
  CanHandler(CanHandler&&) = delete;                            // Define move constructor.
  CanHandler& operator=(CanHandler&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Initializes the CAN bus with the specified baud rate.
  /// @param canBaud Baud rate for CAN communication.
  /// @return `true` if successful, `false` otherwise.
  bool beginSimple(uint32_t canBaud);

  /// @brief Handles ongoing CAN communication in a non-blocking loop.
  /// @return `true` if successful, `false` otherwise.
  bool loopSimple();

  /// @brief Interrupt handler for tracking received CAN frames.
  static inline void rxInterrupt() { intCount++; }

  /// @brief Sends the firmware version over CAN.
  /// @return `true` if successful, `false` otherwise.
  bool sendFwVersion();

  static constexpr uint8_t rxBufferSize = 5U;                   // Size of the receive buffer.
  static constexpr uint16_t pingTime = 1500U;                   // Ping timeout in milliseconds.
  static constexpr uint16_t flashJedecId = 0xEF40U;             // JEDEC ID for Windbond 64Mbit flash.

  HardwareSerial& serialPort;                                   // Reference to the serial driver object.
  uint16_t localCanId;                                          // Local CAN address.
  EEPROMHandler<uint16_t, 0> eepromHandler;                     // EEPROM handler for address persistence.
  const uint8_t ledPin;                                         // LED control pin.
  SPIFlash flash;                                               // SPI flash module driver object.
  static volatile uint8_t intCount;                             // Interrupt counter for received CAN frames.
  void (*canCallback)(uint16_t command, const uint8_t (&data)[8]) = nullptr;  // Callback function pointer.
  OTA ota;                                                      // OTA update handler.
};
#endif // CAN_HANDLER_HPP