#ifndef OTA_HPP
#define OTA_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <SPIFlash.h>                                               /// SPI FLASH module driver.
#include "crc16.hpp"                                                /// CRC16 calculator class.

#ifndef FW_PIECE_SIZE
#define FW_PIECE_SIZE 4U                                            // Default size of firmware pieces for chunked updates.
#endif

/// @brief Class to handle Over-The-Air (OTA) firmware updates using SPI Flash.
/// @details Manages storing, validating, and applying firmware updates in chunks,
/// ensuring data integrity via CRC16 checks.
class OTA final {
private:
  // Size of a single flash block in bytes.
  static constexpr uint16_t flashBlockTobytes = static_cast<uint16_t>(32U * 1024U);

#ifndef PROGRAM_MEMORY_SIZE
  static_assert(false, "PROGRAM_MEMORY_SIZE macro is not defined!");
#else
  /// @brief Total program memory size, excluding the bootloader size.
  static constexpr uint32_t programMemorySize = static_cast<uint32_t>(PROGRAM_MEMORY_SIZE);
#endif

public:
  // Ensures firmware piece size is within valid limits.
  static_assert(FW_PIECE_SIZE < UINT8_MAX, "FW_PIECE_SIZE macro is too large!");

  // Firmware piece size for chunked updates.
  static constexpr uint8_t fwPieceSize = static_cast<uint8_t>(FW_PIECE_SIZE);

  /// @brief OTA update states.
  enum class OtaState : uint8_t {
    IDLE = 0,           // No OTA operation in progress.
    START,              // Starting the OTA process.
    STORE,              // Storing firmware chunks.
    CHECK,              // Validating firmware CRC.
    VALID,              // Firmware is valid.
    INVALID             // Firmware is invalid.
  };

  /// @brief Constructs the OTA handler with the specified SPI Flash reference.
  /// @param flash Reference to the SPI Flash object for firmware storage.
  OTA(SPIFlash& flash);

  /// @brief Default destructor.
  virtual ~OTA() = default;

  /// @brief Initializes the OTA process.
  /// @param flashBlockNumber Flash block number to store the firmware.
  /// @param fwSize Size of the firmware in bytes.
  /// @param fwCrc Expected CRC16 checksum of the firmware.
  /// @return `true` if the OTA process starts successfully, `false` otherwise.
  bool start(uint16_t flashBlockNumber, uint32_t fwSize, uint16_t fwCrc);

  /// @brief Stores the next firmware chunk.
  /// @param dataAddress Expected address of the firmware chunk.
  /// @param fwData Firmware chunk data array.
  /// @return `true` if the data is stored successfully, `false` otherwise.
  bool storeNextData(uint32_t dataAddress, const uint8_t (&fwData)[fwPieceSize]);

  /// @brief Runs the OTA state machine to handle firmware updates.
  /// @return The current state of the OTA process.
  OtaState run();

  /// @brief Checks if the firmware stored in Flash belongs to this device.
  /// @return `true` if the firmware is for this device, `false` otherwise.
  bool isOwnFw() const;

  OTA(const OTA&) = delete;                                         // Define copy constructor.
  OTA& operator=(const OTA&) = delete;                              // Define copy assignment operator.
  OTA(OTA&&) = delete;                                              // Define move constructor.
  OTA& operator=(OTA&&) = delete;                                   // Define move assignment operator.

private:
  SPIFlash& flash;                                                  // Reference to the SPI Flash handler.
  uint8_t firstFwBytes[2];                                          // Stores the first two bytes of the firmware.
  uint32_t fwSize;                                                  // Size of the firmware in bytes.
  uint16_t fwCrc;                                                   // CRC16 checksum of the firmware.
  uint32_t flashPointer;                                            // Current write/read address in Flash.
  Crc16 crc16;                                                      // CRC16 calculator for validation.
  // Calculate the begin address of the FLASH. (FLASH is used in 32KB chunks by this class.)
  // The 0. chunk must contain the OTA FW for this device and the other chunks can contain anything else.
  uint32_t flashBlockBeginAddress;                                  // Start address of the Flash block for firmware.
  OtaState otaState;                                                // Current state of the OTA process.
};
#endif // OTA_HPP