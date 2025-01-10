#pragma once
#ifdef ESP32
#include "canHandlerBase.hpp"                                       /// Base class for CAN handling.
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <HardwareSerial.h>                                         /// Serial driver object.
#include <vector>                                                   /// STL vector for dynamic arrays.
#include "taskHandler.hpp"                                          /// Class for task scheduling.
#include "freertos/queue.h"                                         /// FreeRTOS API for queue management.
#include "freertos/portmacro.h"                                     /// FreeRTOS API for port-specific macros.
#include <freertos/semphr.h>                                        /// FreeRTOS API for semaphore handling.
#include <freertos/projdefs.h>                                      /// FreeRTOS project-wide definitions and configurations.

class CanBase;                                                      // Forward declaration of the `CanBase` class.

/// @brief Handles CAN communication specifically for the ESP32 platform.
class CanHandlerEsp32 final : public CanHandlerBase {
private:
  static constexpr uint32_t defaultCanBaudRate = 500000U;                 // Default CAN bus speed (500 Kb/s).
  static constexpr uint8_t canRxQueueSize = 100U;                         // Size of the RX queue for incoming CAN frames.
  static constexpr uint8_t canTxQueueSize = 100U;                         // Size of the TX queue for outgoing CAN frames.
  static constexpr TickType_t canTxQueueTimeout = pdMS_TO_TICKS(50U);     // Timeout for TX queue operations (50 ms).
  static constexpr TickType_t semaphoreTimeout = pdMS_TO_TICKS(5U);       // Timeout for semaphore operations (5 ms).

public:
  /// @brief Constructor for CanHandlerEsp32.
  /// @param serial Reference to the hardware serial driver object.
  CanHandlerEsp32(HardwareSerial& serial);

  /// @brief Default destructor.
  ~CanHandlerEsp32() = default;

  /// @brief Initializes the CAN handler with the default baud rate.
  /// @return `true` if operations are successful, `false` otherwise.
  virtual bool init() override {
    return init(defaultCanBaudRate);
  }

  /// @brief Handles ongoing CAN communication in a non-blocking loop.
  /// @return `true` if operations are successful, `false` otherwise.
  virtual bool run() override;

  /// @brief Sends a CAN frame.
  /// @param frameOut The CAN frame to send.
  /// @return `true` if the frame was successfully sent, `false` otherwise.
  bool send(const CanFrame& frameOut) const;

  /// @brief Sends a CAN frame with a specified command and data payload.
  /// @param command 10-bit command value.
  /// @param data Array of 8 bytes containing the payload.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  virtual bool send(uint16_t command, const uint8_t (&data)[8]) const override;

  /// @brief Registers a callback for a CAN device.
  /// @param canBasePtr Pointer to the CAN device.
  /// @return `true` if the callback was successfully registered, `false` otherwise.
  bool registerCallback(CanBase* canBasePtr);

  CanHandlerEsp32(const CanHandlerEsp32&) = delete;                       // Define copy constructor.
  CanHandlerEsp32& operator=(const CanHandlerEsp32&) = delete;            // Define copy assignment operator.
  CanHandlerEsp32(CanHandlerEsp32&&) = delete;                            // Define move constructor.
  CanHandlerEsp32& operator=(CanHandlerEsp32&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Initializes the CAN bus with the specified baud rate.
  /// @param canBaud Baud rate for CAN communication.
  /// @return `true` if initialization was successful, `false` otherwise.
  bool init(uint32_t canBaud);

  /// @brief Interrupt service routine for CAN RX events.
  /// @param packetsNum Number of packets available in the RX buffer.
  static IRAM_ATTR void rxInterrupt(int packetsNum);

  static IRAM_ATTR QueueHandle_t canRxQueue;                              // Queue for received CAN frames.

  HardwareSerial& serialPort;                                             // Reference to the serial driver object.
  QueueHandle_t canTxQueue;                                               // Queue for transmitting CAN frames.
  std::vector<CanBase*> canDevicesList;                                   // List of registered CAN devices.
  SemaphoreHandle_t canDevicesListMutex;                                  // Mutex for accessing the CAN devices list.
};
using CanHandler = CanHandlerEsp32;                                       // Alias `CanHandler` to `CanHandlerEsp32`.

/// @brief Base class for CAN devices that interact with the CAN handler.
class CanBase : public virtual Task {
public:
  /// @brief Pure virtual function to initialize the CAN device.
  /// @return `true` if operations are successful, `false` otherwise.
  [[nodiscard]] virtual bool init() = 0;

  /// @brief Pure virtual function to handle ongoing operations for the CAN device.
  /// @return `true` if operations are successful, `false` otherwise.
  [[nodiscard]] virtual bool run() = 0;

  /// @brief Checks if a client CAN ID is valid.
  /// @param clientCanId The client CAN ID to validate.
  /// @return `true` if the CAN ID is valid, `false` otherwise.
  [[nodiscard]] inline bool isClientCanIdValid(uint16_t clientCanId) {
    const bool isLocalCanId = (clientCanId == canHandler.getLocalCanId());
    const bool isMasterCanId = (clientCanId == canHandler.getMasterCanId());
    return (!isLocalCanId && !isMasterCanId);
  }

  /// @brief Gets the client CAN ID.
  /// @return The client CAN ID.
  [[nodiscard]] inline uint16_t getClientCanId() const { return clientCanId; }

  /// @brief Callback function for received CAN frames.
  /// @param canFrame The received CAN frame.
  virtual void canFrameArrivedCallback(const CanHandler::CanFrame& canFrame) = 0;

  CanBase(const CanBase&) = delete;                       // Define copy constructor.
  CanBase& operator=(const CanBase&) = delete;            // Define copy assignment operator.
  CanBase(CanBase&&) = delete;                            // Define move constructor.
  CanBase& operator=(CanBase&&) = delete;                 // Define move assignment operator

protected:
  /// @brief Constructor for the CanBase class.
  /// @param canHandler Reference to the CAN handler.
  /// @param clientCanId Client CAN ID for the device.
  CanBase(CanHandler& canHandler, uint16_t clientCanId) :
    canHandler(canHandler),
    clientCanId(clientCanId)
  {
    if(isClientCanIdValid(this->clientCanId)) {
      this->canHandler.registerCallback(this);
    }
  }

  /// @brief Virtual destructor of the object.
  virtual ~CanBase() = default;

  /// @brief Sends a CAN frame with a specified command and data payload.
  /// @param command 10-bit command value representing the specific action or request.
  /// @param data Array of 8 bytes containing the payload.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  [[nodiscard]] inline bool sendCanFrame(uint16_t command, const uint8_t (&data)[8]) const {
    return canHandler.send(CanHandler::CanFrame{getClientCanId(), command, canHandler.getLocalCanId(), data});
  }

  /// @brief Sends a CAN frame with a command and data payload, using a `CanCmd` enum for the command.
  /// @param command A `CanCmd` value representing the specific action or request.
  /// @param data Array of 8 bytes containing the payload.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  [[nodiscard]] inline bool sendCanFrame(CanCmd command, const uint8_t (&data)[8]) const {
    return sendCanFrame(static_cast<uint16_t>(command), data);
  }

  /// @brief Sends a CAN frame with a specified command and an empty data payload.
  /// @param command 10-bit command value representing the specific action or request.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  [[nodiscard]] inline bool sendCanFrame(uint16_t command) const {
    uint8_t data[8] = {0U};
    return sendCanFrame(command, data);
  }

  /// @brief Sends a CAN frame with a command and an empty data payload, using a `CanCmd` enum for the command.
  /// @param command A `CanCmd` value representing the specific action or request.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  [[nodiscard]] inline bool sendCanFrame(CanCmd command) const {
    return sendCanFrame(static_cast<uint16_t>(command));
  }

  /// @brief Sends a CAN response frame with a specified command and a single boolean response value.
  /// @param command 10-bit command value representing the specific action or request.
  /// @param response Boolean value indicating the response to the command (`true` or `false`).
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  [[nodiscard]] bool sendCanResponse(uint16_t command, bool response) const {
    const uint8_t data[8] = {static_cast<uint8_t>(response), 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    return sendCanFrame(command, data);
  }

  /// @brief Sends a CAN response frame with a command and a single boolean response value,
  /// using a `CanCmd` enum for the command.
  /// @param command A `CanCmd` value representing the specific action or request.
  /// @param response Boolean value indicating the response to the command (`true` or `false`).
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  [[nodiscard]] bool sendCanResponse(CanCmd command, bool response) const {
    return sendCanResponse(static_cast<uint16_t>(command), response);
  }

private:
  CanHandler& canHandler;                                 // Reference to the CAN handler instance.
  const uint16_t clientCanId;                             // Client CAN ID for this device.
};

// #include "crc16.hpp"                                                /// CRC16 calculator class.
// #include <LittleFS.h>                                               /// Use FLASH file system.
// #include "connectivity.hpp"                                         /// Handles the MQTT connection.

// class CanComBase : protected MqttBase {
// private:
//   enum class TransferState : uint8_t {
//     IDLE = 0,
//     START,
//     START_ACK,
//     STORE,
//     STORE_ACK,
//     END_ACK,
//     VALID,
//     INVALID
//   };

//   bool startOta(const char* fileName);
//   void runOta();

//   void* operator new(size_t size);              // Disable new operator.
//   File receivedFile;
//   uint32_t frameNumber;
//   uint16_t storageNumber;
//   char fileName[28];
//   uint32_t fileSize;
//   uint16_t fileCrc;
//   TransferState transferState;
//   Crc16 crc16;
//   static constexpr uint32_t otaTimeoutTime = 2U * 60U * 1000U;
//   SoftwareTimer otaTimeoutTimer;
// };
#endif // ESP32