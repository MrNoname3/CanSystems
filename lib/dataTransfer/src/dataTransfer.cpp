#include "dataTransfer.hpp"
#include <LittleFS.h>                                               /// Use FLASH filesystem.
#include "crc32.hpp"
#include "base64.hpp"
#include "common.hpp"                                               /// Common definitions and functions.

const char DataTransfer::FILE_TRANSFER_PREFIX[] PROGMEM = "[FT]";

DataTransfer::DataTransfer(HardwareSerial& serial) :
  serialPort(serial),
  fileSize_(0U),
  fileCrc_(0U),
  nextFilePieceNumber_(-1),
  remainingFileSize_(0U),
  fileName_(nullptr),
  fileTransferStarted_(false) {}

bool DataTransfer::begin(uint32_t fileSize, uint32_t fileCrc, const char* fileName) {
  if(fileTransferStarted_) { stop(true); }
  fileTransferStarted_ = true;
  fileSize_ = fileSize;
  fileCrc_ = fileCrc;
  nextFilePieceNumber_ = 0;
  remainingFileSize_ = fileSize;
  if(fileName == nullptr) { stop(true); return false; }
  fileName_ = fileName;
  {
#ifdef ESP8266
    FSInfo fsInfo;
    LittleFS.info(fsInfo);
    const uint32_t freeSpace = fsInfo.totalBytes - fsInfo.usedBytes;
#elif defined ESP32
    const uint32_t freeSpace = LittleFS.totalBytes() - LittleFS.usedBytes();
#endif
    const bool isEnoughFreeSpace = freeSpace > fileSize_;
    if(!isEnoughFreeSpace) {
      serialPort.printf_P(PSTR("%s Not enough free space!\r\n  Available: %u\r\n  Required: %u\r\n"), FILE_TRANSFER_PREFIX, freeSpace, fileSize_);
      return false;
    }
  }
  const bool fileExists = LittleFS.exists(FPSTR(fileName_));
  if(fileExists) {
    const bool rmFileResult = LittleFS.remove(FPSTR(fileName_));
    if(!rmFileResult) {
      serialPort.printf_P(PSTR("%s Deleting failed: %s\r\n"), FILE_TRANSFER_PREFIX, fileName_);
      stop(true);
      return false;
    }
  }
  serialPort.printf_P(PSTR("%s File transfer started:\r\n  Name: %s\r\n  Size: %u\r\n  CRC32: %u\r\n"), FILE_TRANSFER_PREFIX, fileName_, fileSize_, fileCrc_);
  if(fileSize == 0) { stop(true); return false; }
  return true;
}

bool DataTransfer::stop(bool deleteFile) {
  fileTransferStarted_ = false;
  fileSize_ = 0;
  fileCrc_ = 0;
  nextFilePieceNumber_ = -1;
  remainingFileSize_ = 0;

  serialPort.printf_P(PSTR("%s File transfer stopped, cleaning up done!\r\n"), FILE_TRANSFER_PREFIX);
  const bool fileExists = LittleFS.exists(FPSTR(fileName_));
  if(fileExists && deleteFile) {
    const bool rmFileResult = LittleFS.remove(FPSTR(fileName_));
    if(!rmFileResult) {
      serialPort.printf_P(PSTR("%s Deleting failed: %s\r\n"), FILE_TRANSFER_PREFIX, fileName_);
      return false;
    }
  }
  fileName_ = nullptr;
  return true;
}

bool DataTransfer::storeBase64(uint32_t filePieceNumber, const char* fileData) {
  if(!fileTransferStarted_) { return false; }
  if(filePieceNumber != nextFilePieceNumber_) { return false; }
  if(remainingFileSize_ == 0) { return false; }
  constexpr uint16_t maxB64Length = receivedFilePieceSize * 4 / 3;
  const uint32_t filePieceB64Size = strnlen(fileData, maxB64Length);
  if(filePieceB64Size == 0) { return false; }

  uint8_t decodedData[receivedFilePieceSize];
  const uint32_t decodedPreSize = Base64::decodedLength(reinterpret_cast<const uint8_t*>(fileData), filePieceB64Size);
  if(decodedPreSize > sizeof(decodedData)) {
    serialPort.printf_P(PSTR("%s File piece size error!\r\n"), FILE_TRANSFER_PREFIX);
    return false;
  }
  const uint32_t decodedPostSize = Base64::decodeBase64(reinterpret_cast<const uint8_t*>(fileData), decodedData, filePieceB64Size);
  if(decodedPreSize != decodedPostSize) {
    serialPort.printf_P(PSTR("%s Decoded size check error!\r\n"), FILE_TRANSFER_PREFIX);
    return false;
  }
  const bool storingResult = store(filePieceNumber, decodedData, decodedPreSize);
  if(!storingResult) {
    serialPort.printf_P(PSTR("%s File storing failed!\r\n"), FILE_TRANSFER_PREFIX);
  }
  return storingResult;
}

bool DataTransfer::store(uint32_t filePieceNumber, const uint8_t* fileData, uint16_t fileDataSize) {
  if(!fileTransferStarted_) { return false; }
  if(filePieceNumber != nextFilePieceNumber_) { return false; }
  if(fileDataSize == 0) { return false; }
  if(remainingFileSize_ == 0) { return false; }

  File receivedFile = LittleFS.open(FPSTR(fileName_), "a");
  if(!receivedFile) {
    serialPort.printf_P(PSTR("%s Opening failed: %s\r\n"), FILE_TRANSFER_PREFIX, fileName_);
    receivedFile.close();
    return false;
  }
  const uint32_t writtenBytes = receivedFile.write(fileData, fileDataSize);
  receivedFile.close();
  if(writtenBytes != fileDataSize) {
    serialPort.printf_P(PSTR("%s Writing failed: %s\r\n"), FILE_TRANSFER_PREFIX, fileName_);
    return false;
  }
  nextFilePieceNumber_++;
  remainingFileSize_ -= fileDataSize;
  return true;
}

bool DataTransfer::checkValidity() {
  if(!fileTransferStarted_) { return false; }
  if(remainingFileSize_ != 0) { return false; }
  File receivedFile = LittleFS.open(FPSTR(fileName_), "r");
  serialPort.printf_P(PSTR("%s Checking received file: %s\r\n"), FILE_TRANSFER_PREFIX, Str::getStateStr(receivedFile));
  if(!receivedFile) { receivedFile.close(); return false; }
  const bool fileSizeOk = (receivedFile.size() == fileSize_);
  serialPort.printf_P(PSTR("  Size -> %s\r\n"), Str::getStateStr(fileSizeOk));
  if(!fileSizeOk) { receivedFile.close(); return false; }
  Crc32 crc32;
  while(receivedFile.available() > 0) { crc32.next(receivedFile.read()); }
  const uint32_t calcFileCrc32 = crc32.get();
  const bool fileCrcOk = (calcFileCrc32 == fileCrc_);
  serialPort.printf_P(PSTR("  CRC -> %s\r\n"), Str::getStateStr(fileCrcOk));
  if(!fileCrcOk) { receivedFile.close(); return false; }
  // if(fileName_ != otaFwLocation) {
  //   receivedFile.close();
  //   stop(false);
  //   return true;
  // }

  receivedFile.seek(0, SeekSet);
  const bool updateBeginResult = Update.begin(fileSize_);
  serialPort.printf_P(PSTR("  Begin -> %s\r\n"), Str::getStateStr(updateBeginResult));
  if(!updateBeginResult) { receivedFile.close(); return false; }
  const bool updateStreamResult = (Update.writeStream(receivedFile) == fileSize_);
  serialPort.printf_P(PSTR("  Stream -> %s\r\n"), Str::getStateStr(updateStreamResult));
  if(!updateStreamResult) { receivedFile.close(); return false; }
  receivedFile.close();
  const bool updateEndResult = Update.end();
  serialPort.printf_P(PSTR("  End -> %s\r\n"), Str::getStateStr(updateEndResult));
  stop(true);
  if(!updateEndResult) { return false; }
  return true;
}