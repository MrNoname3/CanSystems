#ifndef CAN_HANDLER_BASE_HPP
#define CAN_HANDLER_BASE_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "taskHandler.hpp"                                          /// Class for task scheduling.
#include "eepromHandler.hpp"                                        /// EEPROM wrapper class.
#include <string.h>                                                 /// String / byte array handling.

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
  LOOP_TIME_MAX,                              // Send maximum loop time in milliseconds.

  PLAY_MP3 = CBS,                             // Command to play an MP3 file on the node.
  READ_HUM_TEMP_LDR,                          // Read humidity, temperature, and light sensor values.
  HUM_TEMP_SENSOR_ERROR,                      // Error occurred while using the I2C sensor.

  ADD_IRRIGATION = CBS,                       // Add a new irrigation task to the queue.
  SKIP_IRRIGATION,                            // Skip the currently scheduled irrigation task.
  STOP_IRRIGATION,                            // Stop all ongoing and scheduled irrigation tasks.
  IRRIGATION_ERROR,                           // Report an error during irrigation.
  MOISTURE_DATA,                              // Provide moisture level data from sensors.
};

/// @brief Abstract base class for handling CAN communication.
class CanHandlerBase : public Task {
private:
  static constexpr uint32_t canIdFilterMask = 0x3FFUL;              // Mask for lower 10 bits (0b1111111111).

  /// @brief Structure for representing CAN IDs.
  struct __attribute__((packed)) CanId {
    uint16_t master;                          // Master node CAN ID.
    uint16_t local;                           // Local node CAN ID.

    /// @brief Default constructor.
    CanId() :
      master(0U),
      local(0U)
    {}

    /// @brief Parameterized constructor.
    /// @param master Master node CAN ID.
    /// @param local Local node CAN ID.
    CanId(uint16_t master, uint16_t local) :
      master(master),
      local(local)
    {}
  };

public:
  /// @brief Structure representing a CAN frame.
  /// Includes an extended CAN ID with 3 parts: receiver address (`to`), command (`cmd`), and sender address (`from`).
  /// Also includes a data payload that can hold up to 8 bytes.
  struct __attribute__((packed)) CanFrame {
    union {
      uint32_t extId;                         // Extended CAN ID.
      struct {
        uint32_t to : 10;                     // 10 bits for the receiver address.
        uint32_t cmd : 9;                     // 9 bits for the command.
        uint32_t from : 10;                   // 10 bits for the sender address.
        uint32_t padding : 3;                 // Padding to fill up to 32 bits.
      };
    };
    uint8_t data[8];                          // Data payload for the CAN frame (up to 8 bytes).

    /// @brief Default constructor initializing the `extId` and `data` fields to zero.
    CanFrame() : extId(0U), data{0U} {}

    /// @brief Constructor to initialize a CAN frame using an extended CAN ID and data payload.
    /// @param extId The 32-bit extended CAN ID.
    /// @param canData The 8-byte array containing the payload.
    CanFrame(uint16_t extId, const uint8_t (&canData)[8]) :
      extId(static_cast<uint32_t>(extId))
    {
      memcpy(data, canData, sizeof(data));
    }

    /// @brief Constructor to initialize a CAN frame using individual ID components and a data payload.
    /// @param to The receiver address (10 bits).
    /// @param cmd The command identifier (9 bits).
    /// @param from The sender address (10 bits).
    /// @param canData The 8-byte array containing the payload.
    CanFrame(uint16_t to, uint16_t cmd, uint16_t from, const uint8_t (&canData)[8]) :
      extId(((static_cast<uint32_t>(to) & 0x3FF) << 0U) | ((static_cast<uint32_t>(cmd) & 0x1FF) << 10U) | ((static_cast<uint32_t>(from) & 0x3FF) << 19U))
    {
      memcpy(data, canData, sizeof(data));
    }
  };

  /// @brief Enum representing response statuses.
  enum class Response : uint8_t {
    NACK = 0U,                                // Negative acknowledgment.
    ACK                                       // Acknowledgment.
  };

  /// @brief Initializes the CAN handler.
  /// @return `true` if initialization succeeds, `false` otherwise.
  [[nodiscard]] bool init() override = 0;

  /// @brief Executes the task logic.
  /// @return `true` if the task ran successfully, `false` otherwise.
  [[nodiscard]] bool run() override = 0;

  /// @brief Sends a CAN frame with a specified command and data payload.
  /// @param command 10-bit command value representing the specific action or request.
  /// @param data Array of 8 bytes containing the payload.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  virtual bool send(uint16_t command, const uint8_t (&data)[8]) const = 0;

  /// @brief Sends a CAN frame with a specified command.
  /// @param command 10-bit command value representing the specific action or request.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  inline bool send(uint16_t command) const {
    const uint8_t data[8] = {0U};
    return send(command, data);
  }

  /// @brief Sends a CAN frame using an enumeration command and data payload.
  /// @param command Enum value of `CanCmd`.
  /// @param data Array of 8 bytes containing the payload.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  inline bool send(CanCmd command, const uint8_t (&data)[8]) const {
    return send(static_cast<uint16_t>(command), data);
  }

  /// @brief Sends a CAN frame using an enumeration command.
  /// @param command Enum value of `CanCmd`.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  inline bool send(CanCmd command) const {
    return send(static_cast<uint16_t>(command));
  }

  /// @brief Sends a CAN frame with a command and a response.
  /// @param command Enum value of `CanCmd`.
  /// @param response Enum value of `Response`.
  /// @return `true` if the frame was sent successfully, `false` otherwise.
  inline bool send(CanCmd command, Response response) const {
    const uint8_t data[8] = {static_cast<uint8_t>(response), 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    return send(command, data);
  }

  /// @brief Retrieves the CAN ID filter mask.
  /// @return The CAN ID filter mask.
  static constexpr uint32_t getCanIdFilterMask() { return canIdFilterMask; }

  /// @brief Calculate the mask to ignore the upper bits of the extended CAN ID and only consider the lower 10 bits.
  /// @return Filtered local CAN ID.
  inline uint32_t getCanFilteredId() const {
    const uint32_t filteredId = getLocalCanId() & canIdFilterMask;
    return filteredId;
  }

  /// @brief Retrieves the master CAN ID.
  /// @return Master CAN ID.
  inline uint16_t getMasterCanId() const { return canId.master; }

  /// @brief Retrieves the local CAN ID.
  /// @return Local CAN ID.
  inline uint16_t getLocalCanId() const { return canId.local; }

  /// @brief Checks if the device is the master node.
  /// @return `true` if the device is the master node, `false` otherwise.
  inline bool isDeviceMaster() const { return (canId.master == canId.local); }

  CanHandlerBase(const CanHandlerBase&) = delete;                       // Define copy constructor.
  CanHandlerBase& operator=(const CanHandlerBase&) = delete;            // Define copy assignment operator.
  CanHandlerBase(CanHandlerBase&&) = delete;                            // Define move constructor.
  CanHandlerBase& operator=(CanHandlerBase&&) = delete;                 // Define move assignment operator.

protected:
  /// @brief Default constructor for derived classes.
  CanHandlerBase() :
    canId(),
    eepromHandler(&canId)
  {}

  /// @brief Default destructor.
  ~CanHandlerBase() = default;

  /// @brief Loads the CAN IDs from EEPROM.
  /// @return `true` if the CAN IDs were loaded successfully, `false` otherwise.
  inline bool loadCanIds() { return eepromHandler.load(); }

  /// @brief Saves the CAN IDs to EEPROM.
  /// @param master Master CAN ID.
  /// @param local Local CAN ID.
  /// @return `true` if the CAN IDs were saved successfully, `false` otherwise.
  inline bool saveCanIds(uint16_t master, uint16_t local) {
    setCanIds(master, local);
    return eepromHandler.save();
  }

private:
  /// @brief Sets the master and local CAN IDs.
  /// @param master Master CAN ID.
  /// @param local Local CAN ID.
  inline void setCanIds(uint16_t master, uint16_t local) {
    master &= canIdFilterMask;
    local &= canIdFilterMask;
    canId = CanId(master, local);
  }

  CanId canId;                                // Stores the master and local CAN IDs.
  EEPROMHandler<CanId, 0U> eepromHandler;     // EEPROM handler for address persistence.
};
#endif // CAN_HANDLER_BASE_HPP