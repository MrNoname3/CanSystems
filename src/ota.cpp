#include "ota.hpp"
#include <ArduinoJson.h>                      /// Handle JSON files.
#include <LittleFS.h>                         /// Use FLASH filesystem.
#include "crc32.hpp"
#include <Updater.h>
#include "connectivity.hpp"

const char OTA::OTA_PREFIX[] PROGMEM              = "[OTA] ";
const char OTA::OTA_FW_LOCATION[] PROGMEM         = "/config/espFirmware.bin";

OTA::OTA(Stream* serial) : serialPort(serial), fileName_(nullptr) {}

bool OTA::begin(uint32_t fwSize, uint32_t fwCrc, const char* fileName) {
  this->fwSize = fwSize;
  this->fwCrc = fwCrc;
  this->nextFwPieceNumber = 0;
  this->remainingFwSize = fwSize;
  if(!fileName) { return false; }
  this->fileName_ = fileName;

  const bool otaFwExists = LittleFS.exists(FPSTR(fileName_));
  if(otaFwExists) {
    const bool rmFileResult = LittleFS.remove(FPSTR(fileName_));
    if(!rmFileResult) {
      if(serialPort) { serialPort->printf_P(PSTR("%sDeleting failed: %s\r\n"), OTA_PREFIX, fileName_); }
      return false;
    }
  }
  if(serialPort) { serialPort->printf_P(PSTR("%sFW size: %u crc: %u\r\n"), OTA_PREFIX, this->fwSize, this->fwCrc); }
  if(fwSize == 0) { return false; }
  return true;
}

bool OTA::store(uint32_t fwPieceNumber, const uint8_t* fwData, uint16_t fwDataSize) {
  if(!fileName_) { return false; }
  if(fwPieceNumber != nextFwPieceNumber) { return false; }
  if(fwDataSize == 0) { return false; }
  if(remainingFwSize == 0) { return false; }

  File fwFile = LittleFS.open(FPSTR(fileName_), "a");
  if(!fwFile) {
    if(serialPort) { serialPort->printf_P(PSTR("%sOpening failed: %s\r\n"), OTA_PREFIX, fileName_); }
    return false;
  }
  const uint32_t writtenBytes = fwFile.write(fwData, fwDataSize);
  fwFile.close();
  if(writtenBytes != fwDataSize) {
    if(serialPort) { serialPort->printf_P(PSTR("%sWriting failed: %s\r\n"), OTA_PREFIX, fileName_); }
    return false;
  }
  nextFwPieceNumber++;
  remainingFwSize -= fwDataSize;
  return true;
}

bool OTA::checkValidity() {
  if(!fileName_) { return false; }
  if(remainingFwSize != 0) { return false; }
  File fwFile = LittleFS.open(FPSTR(fileName_), "r");
  if(serialPort) { serialPort->printf_P(PSTR("%sChecking FW file:%s\r\n"), OTA_PREFIX, fwFile ? Connectivity::OK_STATE : Connectivity::ERR_STATE); }
  if(!fwFile) { fwFile.close(); return false; }
  const bool fwSizeOk = (fwFile.size() == fwSize);
  if(serialPort) { serialPort->printf_P(PSTR("  Size ->%s\r\n"), fwSizeOk ? Connectivity::OK_STATE : Connectivity::ERR_STATE); }
  if(!fwSizeOk) { fwFile.close(); return false; }
  Crc32 crc32;
  while(fwFile.available() > 0) { crc32.next(fwFile.read()); }
  const uint32_t calcFwCrc32 = crc32.get();
  const bool fwCrcOk = (calcFwCrc32 == fwCrc);
  if(serialPort) { serialPort->printf_P(PSTR("  CRC ->%s\r\n"), fwCrcOk ? Connectivity::OK_STATE : Connectivity::ERR_STATE); }
  if(!fwCrcOk) { fwFile.close(); return false; }
  if(fileName_ != OTA_FW_LOCATION) { fwFile.close(); return true; }

  fwFile.seek(0, SeekSet);
  const bool updateBeginResult = Update.begin(fwSize, 0, 2, 0);
  if(serialPort) { serialPort->printf_P(PSTR("  Begin ->%s\r\n"), updateBeginResult ? Connectivity::OK_STATE : Connectivity::ERR_STATE); }
  if(!updateBeginResult) { fwFile.close(); return false; }
  const bool updateStreamResult = (Update.writeStream(fwFile) == fwSize);
  if(serialPort) { serialPort->printf_P(PSTR("  Stream ->%s\r\n"), updateStreamResult ? Connectivity::OK_STATE : Connectivity::ERR_STATE); }
  if(!updateStreamResult) { fwFile.close(); return false; }
  fwFile.close();
  const bool updateEndResult = Update.end();
  if(serialPort) { serialPort->printf_P(PSTR("  End ->%s\r\n"), updateEndResult ? Connectivity::OK_STATE : Connectivity::ERR_STATE); }
  if(!updateEndResult) { return false; }
  return true;
}