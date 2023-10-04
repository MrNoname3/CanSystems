#ifndef OTA_HPP
#define OTA_HPP

#include <Arduino.h>                          /// Arduino libraries header.
//#include <SPIMemory.h>
#include <SPIFlash.h>
#include <avr/wdt.h>

template<uint16_t flashBlockNumber, uint8_t fwPieceSize>
class OTA {

public:

  struct __attribute__((packed)) 
  FwPiece {
    uint16_t dataAddress = 0;
    uint8_t fwData[fwPieceSize] = { 0 };
    uint16_t crc = 0;
  };

  /// @brief Default constructor.
  OTA(SPIFlash* flash, uint16_t (*calCrc)(const uint8_t*, uint16_t)) : flash(flash), calCrc(calCrc) { }

  /// @brief Default destructor.
  virtual ~OTA() = default;

  bool start(uint16_t fwSize) {
    addressPointer = fwSize;
    if(addressPointer == 0) { return false; }
    //constexpr uint64_t flashBlockBeginAddress = flashBlockNumber * 32 * 1024;
    //if(!flash->eraseBlock32K(flashBlockBeginAddress)) { return false; }
    flash->blockErase32K(flashBlockBeginAddress);
    return true;
  }

  bool storeNextData(FwPiece* fwPiece) {
    if(calCrc == nullptr) { return false; }
    if(addressPointer == 0) { return false; }
    if(addressPointer != fwPiece->dataAddress) { return false; }
    uint16_t crcReceived = fwPiece->crc;
    fwPiece->crc = 0;
    uint16_t crcCalculated = calCrc(reinterpret_cast<uint8_t*>(fwPiece), sizeof(FwPiece));
    if(crcReceived != crcCalculated) { return false; }

    const uint8_t expectedDataSize = addressPointer / fwPieceSize > 0 ? fwPieceSize : addressPointer;
    const uint16_t actualFlashWriteAddress = addressPointer - expectedDataSize;
    //if(!flash->writeByteArray(flashBlockBeginAddress + actualFlashWriteAddress, fwPiece->fwData, expectedDataSize)) { return false; }
    flash->writeBytes(flashBlockBeginAddress + actualFlashWriteAddress, fwPiece->fwData, expectedDataSize);
    addressPointer = actualFlashWriteAddress;
    return true;
  }

  uint16_t getNeededAddress() {
    return addressPointer;
  }

  bool end() {
    wdt_enable(WDTO_15MS);
    delay(20);
    return true;
  }

  void stop() {
    start(0);
  }

  OTA(const OTA&) = delete;               // Define copy constructor.
  OTA& operator=(const OTA&) = delete;    // Define copy assignment operator.
  OTA(OTA&&) = delete;                    // Define move constructor.
  OTA& operator=(OTA&&) = delete;         // Define move assignment operator.

private:

  SPIFlash* flash;
  uint16_t addressPointer = 0;
  uint16_t (*calCrc)(const uint8_t* data, uint16_t length) = nullptr;
  static constexpr uint32_t flashBlockBeginAddress = flashBlockNumber * 32 * 1024;

};


#endif // OTA_HPP