#ifndef OTA_HPP
#define OTA_HPP

#include <Arduino.h>                          /// Arduino libraries header.
#include <ArduinoJson.h>                      /// Handle JSON files.
#include <LittleFS.h>                         /// Use FLASH filesystem.
#include "crc32.hpp"

class OTA {

public:
  OTA(uint32_t fwSize, uint32_t fwCrc, Stream* serial = nullptr) :
  fwSize(fwSize), fwCrc(fwCrc), serialPort(serial), nextFwPieceNumber(0), remainingFwSize(fwSize) {
    const bool otaFwExists = LittleFS.exists(FPSTR(OTA_FW_LOCATION));
    if(otaFwExists) {
      const bool rmFileResult = LittleFS.remove(FPSTR(OTA_FW_LOCATION));
      if(!rmFileResult) {
        if(serialPort) { serialPort->printf_P(PSTR("%sDeleting failed: %s\r\n"), OTA_PREFIX, OTA_FW_LOCATION); }
        this->~OTA();
      }
    }
    if(serialPort) { serialPort->printf_P(PSTR("%sFW size: %u crc: %u\r\n"), OTA_PREFIX, this->fwSize, this->fwCrc); }
    if(fwSize == 0) { this->~OTA(); }
  }

  /// @brief Destructor of the object.
  virtual ~OTA() = default;
/*
  bool begin(uint32_t fwSize, uint32_t fwCrc) {
    this->fwSize = fwSize;
    this->fwCrc = fwCrc;
    this->nextFwPieceNumber = 0;
    this->remainingFwSize = fwSize;
    isFwFileCheckable = false;

    const bool otaFwExists = LittleFS.exists(FPSTR(OTA_FW_LOCATION));
    if(otaFwExists) {
      const bool rmFileResult = LittleFS.remove(FPSTR(OTA_FW_LOCATION));
      if(!rmFileResult) {
        if(serialPort) { serialPort->printf_P(PSTR("%sDeleting failed: %s\r\n"), OTA_PREFIX, OTA_FW_LOCATION); }
        this->~OTA();
      }
    }
    if(fwSize == 0) { this->~OTA(); }
  }
*/
  bool store(uint32_t fwPieceNumber, const uint8_t* fwData, uint16_t fwDataSize) {
    if(fwPieceNumber != nextFwPieceNumber) { return false; }
    if(fwDataSize == 0) { return false; }
    if(remainingFwSize == 0) { return false; }

    File fwFile = LittleFS.open(FPSTR(OTA_FW_LOCATION), "a");
    if(!fwFile) {
      if(serialPort) { serialPort->printf_P(PSTR("%sOpening failed: %s\r\n"), OTA_PREFIX, OTA_FW_LOCATION); }
      return false;
    }
    const uint32_t writtenBytes = fwFile.write(fwData, fwDataSize);
    fwFile.close();
    if(writtenBytes != fwDataSize) {
      if(serialPort) { serialPort->printf_P(PSTR("%sWriting failed: %s\r\n"), OTA_PREFIX, OTA_FW_LOCATION); }
      return false;
    }
    nextFwPieceNumber++;
    remainingFwSize -= fwDataSize;
    return true;
  }

  bool checkValidity() {
    if(remainingFwSize != 0) { return false; }
    uint32_t calcFwCrc32 = 0;
    checkFwCrc32(&calcFwCrc32);
    const bool isCrcOk = (calcFwCrc32 == fwCrc);
    return isCrcOk;
  }

  static bool checkFwCrc32(uint32_t* fwCrc32) {
    File fwFile = LittleFS.open(FPSTR(OTA_FW_LOCATION), "r");
    if(!fwFile) { return false; }
    Crc32 crc32;
    while(fwFile.available() > 0) { crc32.next(fwFile.read()); }
    fwFile.close();
    *fwCrc32 = crc32.get();
    return true;
  }

  OTA(const OTA&) = delete;                       // Define copy constructor.
  OTA& operator=(const OTA&) = delete;            // Define copy assignment operator.
  OTA(OTA&&) = delete;                            // Define move constructor.
  OTA& operator=(OTA&&) = delete;                 // Define move assignment operator.

private:
  const uint32_t fwSize;
  const uint32_t fwCrc;
  Stream* serialPort;
  uint32_t nextFwPieceNumber;
  uint32_t remainingFwSize;

  static const char PROGMEM OTA_PREFIX[];
  static const char PROGMEM OTA_FW_LOCATION[];

};

const char OTA::OTA_PREFIX[] PROGMEM              = "[OTA] ";
const char OTA::OTA_FW_LOCATION[] PROGMEM         = "/config/espFirmware.bin";

#endif // OTA_HPP