#include "ota.hpp"

OTA::OTA(SPIFlash& flash) :
  flash(flash),
  firstFwBytes{0},
  fwSize(0),
  fwCrc(0),
  flashPointer(0),
  flashBlockBeginAddress(0),
  otaState(OtaState::IDLE)
{}

bool OTA::start(uint16_t flashBlockNumber, uint32_t fwSize, uint16_t fwCrc) {
  if(fwSize == 0) { return false; }                               // No firmware to write, return early.
  if(fwSize > programMemorySize) { return false; }                // Check fw size.
  flash.chipErase();                                              // Attempt to erase the FLASH block.
  this->fwSize = fwSize;                                          // Save FW size.
  this->fwCrc = fwCrc;                                            // Store FW CRC.
  flashBlockBeginAddress = flashBlockNumber * flashBlockTobytes;
  flashPointer = 0;                                               // Reset flash pointer.
  otaState = OtaState::START;
  return true;                                                    // Return success.
}

bool OTA::storeNextData(uint32_t dataAddress, const uint8_t (&fwData)[fwPieceSize]) {
  if(flashPointer >= fwSize) { return false; }               // Check for overwrites.
  if(flashPointer != dataAddress) { return false; }          // Check if the dataAddress matches the expected address.

  // Calculate valid data size, this only matters if less bytes remains than fwPieceSize.
  const uint32_t remainingBytes = fwSize - flashPointer;
  const uint8_t expectedDataSize = remainingBytes < fwPieceSize ? remainingBytes : fwPieceSize;

  // Iterates trough the received FW bytes.
  for(uint8_t i = 0; i < expectedDataSize; i++) {
    // Save the first 2 bytes only in memory for safety reason (bootloader triggers OTA only, if the first 2 byte is a jmp opcode).
    if(flashPointer < sizeof(firstFwBytes)) {
      firstFwBytes[i] = fwData[i];
    }
    else {
      // Save the other bytes to the FLASH.
      flash.writeByte(flashBlockBeginAddress + flashPointer, fwData[i]);
    }
    flashPointer++;
    if(flashPointer > fwSize) { return false; }              // Check for overwrites.
  }
  return true;
}

OTA::OtaState OTA::run() {
  switch(otaState) {
    case OtaState::IDLE: {} break;
    case OtaState::START: {
      if(!flash.busy()) {
        otaState = OtaState::STORE;
      }
    } break;
    case OtaState::STORE: {
      if(flashPointer == fwSize) {
        flashPointer = 0;
        crc16.reset();
        otaState = OtaState::CHECK;
      }
    } break;
    case OtaState::CHECK: {
      if(flashPointer < fwSize) {
        uint8_t readedByte = 0;
        // Read the first bytes from the memory.
        if(flashPointer < sizeof(firstFwBytes)) {
          readedByte = firstFwBytes[flashPointer];
        }
        else { // Read the other bytes from FLASH.
          readedByte = flash.readByte(flashBlockBeginAddress + flashPointer);
        }
        crc16.next(readedByte);
        flashPointer++;
      }
      if(flashPointer == fwSize) {
        if(fwCrc != crc16.get()) {
          otaState = OtaState::INVALID;
          return otaState;
        }
        // If everything is good, write the remaining bytes from memory to FLASH and read it back.
        uint8_t dataReadBack[sizeof(firstFwBytes)] = { 0 };
        flash.writeBytes(flashBlockBeginAddress, firstFwBytes, sizeof(firstFwBytes));
        flash.readBytes(flashBlockBeginAddress, dataReadBack, sizeof(firstFwBytes));
        // Compares the read-back bytes with the original.
        if(memcmp(firstFwBytes, dataReadBack, sizeof(firstFwBytes)) == 0) {
          otaState = OtaState::VALID;
        }
      }
    } break;
    case OtaState::VALID: {
      otaState = OtaState::IDLE;
    } break;
    case OtaState::INVALID: {
      flash.chipErase();
      fwSize = 0;
      fwCrc = 0;
      flashPointer = 0;
      flashBlockBeginAddress = 0;
      otaState = OtaState::IDLE;
    } break;
  }
  return otaState;
}
