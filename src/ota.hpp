#ifndef OTA_HPP
#define OTA_HPP

#include <Arduino.h>                          /// Arduino libraries header.
#include <SPIFlash.h>                         /// FLASH memory driver library.
#include <avr/wdt.h>                          /// Watchdog timer library.

/// @brief OTA update handler class.
/// @tparam flashBlockNumber FLASH begin address: flashBlockNumber * 32KB.
/// @tparam fwPieceSize The size of FW chunks in bytes.
template<uint16_t flashBlockNumber, uint8_t fwPieceSize>
class OTA {

public:

  // Data struct for OTA update.
  struct __attribute__((packed)) 
  FwPiece {
    uint16_t dataAddress;                                               // Serial number of the OTA FW piece.
    uint8_t fwData[fwPieceSize];                                        // FW bytes.
    uint16_t crc;                                                       // CRC of the struct.
    FwPiece() : dataAddress(0), fwData{0}, crc(0) { }
  };

  /// @brief Constructor of OTA handler class.
  /// @param flash Pointer of the SPI FLASH handler object.
  /// @param calCrc Pointer of the 16bit CRC calculator function.
  OTA(SPIFlash* flash, uint16_t (*calCrc)(const uint8_t*, uint16_t)) : flash(flash), fwAddressPointerW(0), fwAddressPointerR(0), calCrc(calCrc) { }

  /// @brief Default destructor.
  virtual ~OTA() = default;

  /// @brief Start the OTA update process.
  /// @param fwSize Size of the new FW.
  /// @return Retruns with the result.
  bool start(uint16_t fwSize) {
    if(fwSize == 0) { return false; }                                   // No firmware to write, return early.
    flash->blockErase32K(flashBlockBeginAddress);                       // Attempt to erase the FLASH block.
    fwAddressPointerW = fwAddressPointerR = fwSize;                     // Update the address pointers.
    return true;                                                        // Return success.
  }

  /// @brief Store the new FW pieces to FLASH from top to bottom.
  /// @param fwPiece Struct which contains the data for FW update.
  /// @return Retruns with the result.
  bool storeNextData(FwPiece* fwPiece) {
    if(calCrc == nullptr) { return false; }                             // Check if the CRC calculation function is available.
    if(fwAddressPointerW == 0) { return false; }                        // Check fwAddressPointerW validity.
    if(fwAddressPointerW != fwPiece->dataAddress) { return false; }     // Check if the dataAddress matches the expected address.

    // Calculate the CRC and compare it with the received CRC.
    uint16_t crcReceived = fwPiece->crc;
    fwPiece->crc = 0;
    uint16_t crcCalculated = calCrc(reinterpret_cast<uint8_t*>(fwPiece), sizeof(FwPiece));
    if(crcReceived != crcCalculated) {
      return false;
    }

    // Calculate the expected data size and actual write address.
    const uint8_t expectedDataSize = fwAddressPointerW < fwPieceSize ? fwAddressPointerW : fwPieceSize;
    const uint16_t actualFlashWriteAddress = fwAddressPointerW - expectedDataSize;

    // Define constants for clarity.
    constexpr uint8_t maxTryCount = 3;

    // Initialize variables.
    uint8_t dataReadBack[fwPieceSize] = { 0 };
    uint16_t actualAddress = flashBlockBeginAddress + actualFlashWriteAddress;

    // Retry writing and reading with a maximum try count.
    for(uint8_t tryCounter = 0; tryCounter < maxTryCount; ++tryCounter) {
      flash->writeBytes(actualAddress, fwPiece->fwData, expectedDataSize);
      flash->readBytes(actualAddress, dataReadBack, expectedDataSize);
      if(memcmp(fwPiece->fwData, dataReadBack, expectedDataSize) == 0) {
        // Data matches, update the address pointer and return success.
        fwAddressPointerW = actualFlashWriteAddress;
        return true;
      }
    }

    // All tries failed, return failure.
    return false;
  }

  /// @brief Reads bytes from FLASH from top to bottom.
  /// @param dataBuffer Byte buffer which contains the queried data.
  /// @return Retruns with the result.
  bool readNextData(uint8_t* dataBuffer) {
    if(fwAddressPointerR == 0) { return false; }                        // Check fwAddressPointerR validity.

    // Calculate the expected data size and actual read address.
    const uint8_t actualSize = fwAddressPointerR < fwPieceSize ? fwAddressPointerR : fwPieceSize;
    fwAddressPointerR = fwAddressPointerR - actualSize;

    // Read the queried bytes from SPI FLASH.
    flash->readBytes(flashBlockBeginAddress + fwAddressPointerR, dataBuffer, actualSize);
    return true;
  }

  /// @brief Get the address of the next FW piece for write.
  /// @return Returns the address.
  uint16_t getAddressW() {
    return fwAddressPointerW;
  }

  /// @brief Get the address of the next FW piece for read.
  /// @return Returns the address.
  uint16_t getAddressR() {
    return fwAddressPointerR;
  }

  /// @brief Restart the MCU after OTA FW update process.
  /// @return If it returns anything something is wrong with the WDT setup.
  bool end() {
    wdt_enable(WDTO_15MS);                                              // Setup watchdog timer.
    delay(20);                                                          // Let the WDT restart the MCU.
    return false;
  }

  /// @brief Stop the OTA FW update process if something went wrong.
  void stop() {
    flash->blockErase32K(flashBlockBeginAddress);                       // Attempt to erase the FLASH block.
    fwAddressPointerW = fwAddressPointerR = 0;                          // Reset address pointers.
  }

  OTA(const OTA&) = delete;                                             // Define copy constructor.
  OTA& operator=(const OTA&) = delete;                                  // Define copy assignment operator.
  OTA(OTA&&) = delete;                                                  // Define move constructor.
  OTA& operator=(OTA&&) = delete;                                       // Define move assignment operator.

private:

  SPIFlash* flash;                                                      // Pointer to SPI FLASH handler.
  uint16_t fwAddressPointerW;                                           // Points to the next piece of the FW at write opeartion.
  uint16_t fwAddressPointerR;                                           // Points to the next piece of the FW at read opeartion.
  uint16_t (*calCrc)(const uint8_t* data, uint16_t length);             // Pointer of the CRC calculator function.
  
  // Calculate the begin address of the FLASH. (FLASH is used in 32KB chunks by this class.)
  // The 0. chunk must contain the OTA FW for this device and the other chunks can contain anything else.
  static constexpr uint32_t flashBlockBeginAddress = flashBlockNumber * 32 * 1024;

};


#endif // OTA_HPP