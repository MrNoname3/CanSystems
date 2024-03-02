#ifndef OTA_HPP
#define OTA_HPP

#include <stdint.h>
#include <SPIFlash.h>                                               /// SPI FLASH module driver.
//#include <avr/wdt.h>                                                /// Watchdog timer library.
#include "../../crc16/src/crc16.hpp"                                /// CRC16 calculator class.
//#include "Reset/DeviceReset.hpp"

/// @brief OTA update handler class.
/// @tparam flashBlockNumber FLASH begin address: flashBlockNumber * 32KB.
/// @tparam fwPieceSize The size of FW chunks in bytes.
template<uint16_t flashBlockNumber, uint8_t fwPieceSize>
class OTA final {
public:
  /// @brief Constructor of OTA handler class.
  /// @param flash Pointer of the SPI FLASH handler object.
  OTA(SPIFlash& flash) :
    flash(flash),
    firstFwBytes{0},
    fwSize(0),
    fwCrc(0),
    flashWritePointer(0)
  {}

  /// @brief Default destructor.
  virtual ~OTA() = default;

  /// @brief Start the OTA update process.
  /// @param fwSize Size of the new FW.
  /// @param fwCrc CRC16 of the new FW.
  /// @return Retruns with the result.
  bool start(uint16_t fwSize, uint16_t fwCrc) {
    if(fwSize == 0) { return false; }                               // No firmware to write, return early.
    constexpr uint16_t maxAllowedSize = 31U * 1024U;                // Max flash size is 32Kb - 1Kb for bootloader.
    if(fwSize > maxAllowedSize) { return false; }                   // Check fw size.
    flash.blockErase32K(flashBlockBeginAddress);                    // Attempt to erase the FLASH block.
    this->fwSize = fwSize;                                          // Save FW size.
    this->fwCrc = fwCrc;                                            // Store FW CRC.
    flashWritePointer = 0;                                          // Reset write pointer.
    return true;                                                    // Return success.
  }

  /// @brief Store the new FW pieces to FLASH from top to bottom.
  /// @param dataAddress Contains the data for FW update.
  /// @param fwData Contains the data for FW update.
  /// @return Retruns with the result.
  bool storeNextData(uint16_t dataAddress, const uint8_t (&fwData)[fwPieceSize]) {
    if(flashWritePointer >= fwSize) { return false; }               // Check for overwrites.
    if(flashWritePointer != dataAddress) { return false; } // Check if the dataAddress matches the expected address.

    // Calculate valid data size, this only matters if less bytes remains than fwPieceSize.
    const uint16_t remainingBytes = fwSize - flashWritePointer;
    const uint8_t expectedDataSize = remainingBytes < fwPieceSize ? remainingBytes : fwPieceSize;
    //wdt_reset();                                                    // Reset the watchdog timer.

    // Iterates trough the received FW bytes.
    for(uint8_t i = 0; i < expectedDataSize; i++) {
      // Save the first 2 bytes only in memory for safety reason (bootloader triggers OTA only, if the first 2 byte is a jmp opcode).
      if(flashWritePointer < sizeof(firstFwBytes)) {
        firstFwBytes[i] = fwData[i];
      }
      else {
        // Save the other bytes to the FLASH.
        flash.writeByte(flashBlockBeginAddress + flashWritePointer, fwData[i]);
      }
      flashWritePointer++;
      if(flashWritePointer > fwSize) { return false; }              // Check for overwrites.
    }
    return true;
  }

  /// @brief Make a FW upgrade after store and check everything.
  /// @return If it returns, something went wrong. On success it should end with WDT reset.
  bool upgrade() {
    bool ret = validityCheck();                                     // Check FW validity.
    if(ret) {
      end();                                                        // On success -> restart the MCU to prefer OTA.
    }
    else {
      stop();                                                       // On failure -> wipe every stored data.
    }
    return ret;
  }

  /// @brief Check the validity of the stored bytes.
  /// @return Returns true if everything is OK.
  bool validityCheck() {
    if(flashWritePointer != fwSize) { return false; }               // Check if FW is fully stored.
    Crc16 calculatedCrc;
    //wdt_reset();                                                    // Reset the watchdog timer.

    // Calculate the CRC of the whole stored FW.
    for(uint16_t flashReadPointer = 0; flashReadPointer < fwSize; flashReadPointer++) {
      uint8_t readedByte = 0;
      // Read the first bytes from the memory.
      if(flashReadPointer < sizeof(firstFwBytes)) {
        readedByte = firstFwBytes[flashReadPointer];
      }
      else {
        // Read the other bytes from FLASH.
        readedByte = flash.readByte(flashBlockBeginAddress + flashReadPointer);
      }
      calculatedCrc.next(readedByte);                               // Calculate CRC for the readed bytes.
    }

    if(fwCrc != calculatedCrc.get()) { return false; }              // Check CRC match.

    // If everything is good, write the remaining bytes from memory to FLASH and read it back.
    uint8_t dataReadBack[sizeof(firstFwBytes)] = { 0 };
    flash.writeBytes(flashBlockBeginAddress, firstFwBytes, sizeof(firstFwBytes));
    flash.readBytes(flashBlockBeginAddress, dataReadBack, sizeof(firstFwBytes));

    // Compares the read-back bytes with the original.
    if(memcmp(firstFwBytes, dataReadBack, sizeof(firstFwBytes)) == 0) {
      return true;
    }
    return false;
  }

  /// @brief Restart the MCU after OTA FW update process.
  void end() {
    fwSize = 0;
    flashWritePointer = 0;                                          // Reset write pointer.
    //DeviceReset::reset();
  }

  /// @brief Stop the OTA FW update process if something went wrong.
  void stop() {
    fwSize = 0;
    flashWritePointer = 0;                                          // Reset write pointer.
    flash.blockErase32K(flashBlockBeginAddress);                    // Attempt to erase the FLASH block.
  }

  OTA(const OTA&) = delete;                                         // Define copy constructor.
  OTA& operator=(const OTA&) = delete;                              // Define copy assignment operator.
  OTA(OTA&&) = delete;                                              // Define move constructor.
  OTA& operator=(OTA&&) = delete;                                   // Define move assignment operator.
private:
  SPIFlash& flash;                                                  // Pointer to SPI FLASH handler.
  uint8_t firstFwBytes[2];                                          // Save FW first bytes to memory.
  uint16_t fwSize;                                                  // Size of the FW.
  uint16_t fwCrc;                                                   // CRC16 of the FW.
  uint16_t flashWritePointer;                                       // Stores the actual FLASH write address.
  // Calculate the begin address of the FLASH. (FLASH is used in 32KB chunks by this class.)
  // The 0. chunk must contain the OTA FW for this device and the other chunks can contain anything else.
  static constexpr uint32_t flashBlockBeginAddress = flashBlockNumber * 32 * 1024;
};
#endif // OTA_HPP
