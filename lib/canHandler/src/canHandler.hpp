#ifndef CAN_HANDLER_HPP
#define CAN_HANDLER_HPP

#if defined(__AVR_ATmega328P__)
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <HardwareSerial.h>                                         /// Serial driver object.
#include "canCommands.hpp"                                          /// Definitions of CAN commands.
#include "eepromHandler.hpp"                                        /// EEPROM wrapper class.
#include <SPIFlash.h>                                               /// SPI FLASH module driver.
#include "ota.hpp"                                                  /// OTA (Over-The-Air) update handler.
#include "taskHandler.hpp"                                          /// Class for task scheduling.
#include "common.hpp"                                               /// Common definitions and functions.
#include "debugLedHandler.hpp"                                      /// Handles the debug LED.

/// @brief Handles CAN communication, and OTA updates.
/// @details The `CanHandler` class manages the CAN communication protocol
/// for interacting with devices. It also facilitates Over-The-Air updates.
class CanHandler final : public Task {
private:
#ifdef NEW_CAN_ADDRESS
  #warning "NEW_CAN_ADDRESS is defined!"
  static_assert(NEW_CAN_ADDRESS < 1023, "New CAN address must be less than 1023!");
  static constexpr uint16_t newCanAddress = static_cast<uint16_t>(NEW_CAN_ADDRESS);
#endif
  static_assert(MASTER_CAN_ADDRESS < 1023, "Master CAN address must be less than 1023!");
  static constexpr uint16_t masterCanId = static_cast<uint16_t>(MASTER_CAN_ADDRESS);
public:
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
  /// @param debugLed Reference to a `DebugLedHandler` object.
  /// @param canCsPin Chip select pin for the CAN module.
  /// @param canIntPin Interrupt pin for the CAN module.
  /// @param flashCsPin Chip select pin for the SPI flash.
  CanHandler(HardwareSerial& serial, DebugLedHandler& debugLed, uint8_t canCsPin, uint8_t canIntPin, uint8_t flashCsPin);

  /// @brief Default destructor.
  ~CanHandler() = default;

  /// @brief Initializes the CAN handler.
  /// @details Sets the CAN bus speed to 500 Kb/s.
  /// @return `true`.
  virtual bool init() override {
    return init(500000U);
  }

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
  bool init(uint32_t canBaud);

  /// @brief Handles ongoing CAN communication in a non-blocking loop.
  /// @return `true` if successful, `false` otherwise.
  bool loopSimple();

  /// @brief Interrupt handler for tracking received CAN frames.
  static inline void rxInterrupt() { intCount++; }

  /// @brief Sends the firmware version over CAN.
  /// @return `true` if successful, `false` otherwise.
  bool sendFwVersion() const;

  static constexpr uint8_t rxBufferSize = 5U;                               // Size of the receive buffer.
  static constexpr uint16_t pingTime = Time::secToMs(2U);                   // Ping timeout in milliseconds.
  static constexpr uint16_t flashJedecId = 0xEF40U;                         // JEDEC ID for Windbond 64Mbit flash.

  static volatile uint8_t intCount;                                         // Interrupt counter for received CAN frames.
  HardwareSerial& serialPort;                                               // Reference to the serial driver object.
  DebugLedHandler& debugLed;                                                // Reference to debug LED handler objec
  uint16_t localCanId;                                                      // Local CAN address.
  EEPROMHandler<uint16_t, 0U> eepromHandler;                                // EEPROM handler for address persistence.
  SPIFlash flash;                                                           // SPI flash module driver object.
  OTA ota;                                                                  // OTA update handler.
  void (*canCallback)(uint16_t command, const uint8_t (&data)[8]);          // Callback function pointer.
  uint32_t eventTimer;                                                      // Class wide variable for universal timings.
  OTA::OtaState lastOtaState;                                               // Store last known OTA state.
};

#elif defined(ESP32)

#include <Arduino.h>                          /// Arduino libraries header.
#include <HardwareSerial.h>
#include <vector>
#include "connectivity.hpp"
#include <LittleFS.h>                         /// Use FLASH filesystem.
#include "canCommands.hpp"
#include "crc16.hpp"

class CanHandler final {
private:
  static_assert(MASTER_CAN_ADDRESS < 1023, "Master CAN address must be less than 1023!");
  static constexpr uint16_t localCanId = static_cast<uint16_t>(MASTER_CAN_ADDRESS);
public:
  class CanComBase;

  struct __attribute__((packed))
  CanFrame {                                    // CAN frame.
    union {
      uint32_t extId;                           // Extended CAN ID.
      struct {
        uint32_t to : 10;                       // 10 bits for receiver address.
        uint32_t cmd : 9;                       // 9 bits for command.
        uint32_t from : 10;                     // 10 bits for sender address.
        uint32_t padding : 3;                   // Padding to fill up to 32 bits.
      };
    };
    uint8_t data[8];                            // CAN data.
    CanFrame() : extId(), data{0} {}
  };

  CanHandler(HardwareSerial& serial);
  /// @brief Destructor of the object.
  virtual ~CanHandler() = default;
  bool begin(uint32_t canBaud);
  bool loop();

  CanHandler(const CanHandler&) = delete;                       // Define copy constructor.
  CanHandler& operator=(const CanHandler&) = delete;            // Define copy assignment operator.
  CanHandler(CanHandler&&) = delete;                            // Define move constructor.
  CanHandler& operator=(CanHandler&&) = delete;                 // Define move assignment operator.
private:
  bool send(const CanFrame& frameOut);
  static void rxInterrupt(int packetsNum) __attribute__((optimize("-O3")));
  bool registerCallback(CanHandler::CanComBase* obj);

  HardwareSerial& serialPort;
  static constexpr uint8_t canRxQueueSize = 10U;
  static constexpr uint8_t canTxQueueSize = 10U;
  static QueueHandle_t canRxQueue;              // Queue handler for CAN RX.
  QueueHandle_t canTxQueue;                     // Queue handler for CAN TX.
  void* operator new(size_t size);              // Disable new operator.
  std::vector<CanHandler::CanComBase*> canDevices;
  static const char PROGMEM OK_STATE[];
  static const char PROGMEM ERR_STATE[];
  static const char PROGMEM CAN_PREFIX[];

public:
  class SoftwareTimer {
  public:
    SoftwareTimer(uint32_t time);
    /// @brief Destructor of the object.
    virtual ~SoftwareTimer() = default;
    bool isExpired();
    void reload();

    SoftwareTimer(const SoftwareTimer&) = delete;                       // Define copy constructor.
    SoftwareTimer& operator=(const SoftwareTimer&) = delete;            // Define copy assignment operator.
    SoftwareTimer(SoftwareTimer&&) = delete;                            // Define move constructor.
    SoftwareTimer& operator=(SoftwareTimer&&) = delete;                 // Define move assignment operator.
  private:
    const uint32_t time_;
    uint32_t start_time_;
  };

public:
  class CanComBase : protected Connectivity::MqttComBase {
  public:
    friend class CanHandler;
    CanComBase(const CanComBase&) = delete;                       // Define copy constructor.
    CanComBase& operator=(const CanComBase&) = delete;            // Define copy assignment operator.
    CanComBase(CanComBase&&) = delete;                            // Define move constructor.
    CanComBase& operator=(CanComBase&&) = delete;                 // Define move assignment operator.
  protected:
    enum class Response : uint8_t {
      NACK = 0,
      ACK
    };

    CanComBase(CanHandler& canHandler, uint32_t canId, Connectivity& connectivity, const char* classID);
    /// @brief Destructor of the object.
    virtual ~CanComBase() = default;
    virtual bool init() = 0;
    virtual bool run() = 0;
    virtual void canFrameReceived(CanHandler::CanFrame& canFrame) = 0;
    bool sendCanFrame(CanCmd command, const uint8_t (&data)[8]) const;
    bool sendCanFrame(uint16_t command, const uint8_t (&data)[8]) const;
    bool sendCanCmd(CanCmd command) const;
    bool sendCanCmd(uint16_t command) const;
  private:
    enum class TransferState : uint8_t {
      IDLE = 0,
      START,
      START_ACK,
      STORE,
      STORE_ACK,
      END_ACK,
      VALID,
      INVALID
    };

    bool beginPriv();
    bool loopPriv();
    void canFrameReceivedPriv(CanHandler::CanFrame& canFrame);
    uint32_t getCanId() const;
    virtual bool begin() override;
    virtual bool loop() override;
    virtual void messageReceived(uint8_t* payload, uint32_t length) override;

    bool startOta(const char* fileName);
    void runOta();

    static constexpr uint32_t pingTime = 500U;
    static constexpr uint32_t alertTime = 1000U;
    CanHandler& canHandler;
    const uint32_t nodeCanId;
    SoftwareTimer pingTimer;
    SoftwareTimer alertTimer;
    bool nodeAlive_;
    static const char PROGMEM CAN_BASE_PREFIX[];
    static const char PROGMEM STATUS_ONLINE[];
    static const char PROGMEM STATUS_OFFLINE[];
    static const char PROGMEM STATUS_RESTARTED[];
    static const char PROGMEM STATUS_FRAME[];
    static const char PROGMEM BUTTON_FRAME[];
    static const char PROGMEM FW_VERSION_FRAME[];
    static const char PROGMEM OTA_FRAME[];

    void* operator new(size_t size);              // Disable new operator.
    File receivedFile;
    uint32_t frameNumber;
    uint16_t storageNumber;
    char fileName[28];
    uint32_t fileSize;
    uint16_t fileCrc;
    TransferState transferState;
    Crc16 crc16;
    static constexpr uint32_t otaTimeoutTime = 2U * 60U * 1000U;
    SoftwareTimer otaTimeoutTimer;
  };

};
#endif
#endif // CAN_HANDLER_HPP