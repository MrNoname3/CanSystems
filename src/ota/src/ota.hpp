#ifndef OTA_HPP
#define OTA_HPP

#include <stdint.h>
#include <SPIFlash.h>                                               /// SPI FLASH module driver.
#include "../../crc16/src/crc16.hpp"                                /// CRC16 calculator class.

#ifndef FW_PIECE_SIZE
#define FW_PIECE_SIZE 4U
#endif

class OTA final {
private:
  static constexpr uint16_t flashBlockTobytes = 32U * 1024U;
#ifndef PROGRAM_MEMORY_SIZE
  static_assert(false, "PROGRAM_MEMORY_SIZE macro is not defined!");
#else
  // Program memory size is: MAX_FLASH_SIZE - 1Kb for the bootloader.
  static constexpr uint32_t programMemorySize = static_cast<uint32_t>(PROGRAM_MEMORY_SIZE);
#endif
public:
  static_assert(FW_PIECE_SIZE < UINT8_MAX, "FW_PIECE_SIZE macro is too large!");
  static constexpr uint8_t fwPieceSize = static_cast<uint8_t>(FW_PIECE_SIZE);
  enum class OtaState : uint8_t {
    IDLE = 0,
    START,
    STORE,
    CHECK,
    VALID,
    INVALID
  };

  OTA(SPIFlash& flash);
  /// @brief Default destructor.
  virtual ~OTA() = default;
  bool start(uint16_t flashBlockNumber, uint32_t fwSize, uint16_t fwCrc);
  bool storeNextData(uint32_t dataAddress, const uint8_t (&fwData)[fwPieceSize]);
  OtaState run();
  bool isOwnFw() const;

  OTA(const OTA&) = delete;                                         // Define copy constructor.
  OTA& operator=(const OTA&) = delete;                              // Define copy assignment operator.
  OTA(OTA&&) = delete;                                              // Define move constructor.
  OTA& operator=(OTA&&) = delete;                                   // Define move assignment operator.
private:
  SPIFlash& flash;                                                  // Pointer to SPI FLASH handler.
  uint8_t firstFwBytes[2];                                          // Save FW first bytes to memory.
  uint32_t fwSize;                                                  // Size of the FW.
  uint16_t fwCrc;                                                   // CRC16 of the FW.
  uint32_t flashPointer;                                            // Stores the actual FLASH write/read address.
  Crc16 crc16;
  // Calculate the begin address of the FLASH. (FLASH is used in 32KB chunks by this class.)
  // The 0. chunk must contain the OTA FW for this device and the other chunks can contain anything else.
  uint32_t flashBlockBeginAddress;
  OtaState otaState;
};
#endif // OTA_HPP