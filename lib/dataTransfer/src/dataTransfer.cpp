#include "dataTransfer.hpp"
#include <LittleFS.h>                                               /// Use FLASH filesystem.
#include "crc32.hpp"
#include "base64.hpp"
#include "common.hpp"                                               /// Common definitions and functions.

const char DataTransfer::FILE_TRANSFER_PREFIX[] PROGMEM = "[FT]";
const char DataTransfer::TEMP_FILE[] PROGMEM = "/temp.tmp";

DataTransfer::DataTransfer(HardwareSerial& serial) :
  serialPort(serial),
  fileSizeLocal(0U),
  fileCrcLocal(0U),
  nextFilePieceNumberLocal(-1),
  remainingFileSizeLocal(0U),
  fileNameLocal{'\0'},
  isFileTransferStarted(false)
{}

bool DataTransfer::begin(uint32_t fileSize, uint32_t fileCrc, const char* fileName) {
  if(fileSize == 0U) { return false; }
  fileSizeLocal = fileSize;
  fileCrcLocal = fileCrc;
  nextFilePieceNumberLocal = 0U;
  remainingFileSizeLocal = fileSize;
  if(fileName == nullptr) { return false; }
  memset(fileNameLocal, '\0', sizeof(fileNameLocal));
  const uint32_t fileNameSize = strlcpy(fileNameLocal, fileName, sizeof(fileNameLocal));
  if(fileNameSize >= sizeof(fileNameLocal) || fileNameSize == 0U) { return false; }
  {
#ifdef ESP8266
    FSInfo fsInfo;
    LittleFS.info(fsInfo);
    const uint32_t freeSpace = fsInfo.totalBytes - fsInfo.usedBytes;
#elif defined ESP32
    const uint32_t freeSpace = LittleFS.totalBytes() - LittleFS.usedBytes();
#endif
    const bool isEnoughFreeSpace = freeSpace > fileSizeLocal;
    if(!isEnoughFreeSpace) {
      serialPort.printf_P(PSTR("%s Not enough free space!\r\n  Available: %u\r\n  Required: %u\r\n"), FILE_TRANSFER_PREFIX, freeSpace, fileSizeLocal);
      return false;
    }
  }
  const bool fileExists = LittleFS.exists(FPSTR(TEMP_FILE));
  if(fileExists) {
    const bool rmFileResult = LittleFS.remove(FPSTR(TEMP_FILE));
    if(!rmFileResult) {
      serialPort.printf_P(PSTR("%s Deleting failed: %s\r\n"), FILE_TRANSFER_PREFIX, TEMP_FILE);
      return false;
    }
  }
  serialPort.printf_P(PSTR("%s File transfer started:\r\n  Name: %s\r\n  Size: %u\r\n  CRC32: %u\r\n"), FILE_TRANSFER_PREFIX, fileNameLocal, fileSizeLocal, fileCrcLocal);
  isFileTransferStarted = true;
  return true;
}

bool DataTransfer::storeBase64(uint32_t filePieceNumber, const char* fileData) {
  if(!isFileTransferStarted) { return false; }
  if(filePieceNumber != nextFilePieceNumberLocal) { return false; }
  if(remainingFileSizeLocal == 0U) { return false; }
  if(fileData == nullptr) { return false; }

  const uint32_t filePieceB64Size = strnlen(fileData, maxB64Length);
  if(filePieceB64Size == 0U || filePieceB64Size == maxB64Length) { return false; }

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
  if(!isFileTransferStarted) { return false; }
  if(filePieceNumber != nextFilePieceNumberLocal) { return false; }
  if(fileDataSize == 0U) { return false; }
  if(remainingFileSizeLocal == 0U) { return false; }
  if(fileData == nullptr) { return false; }

  File receivedFile = LittleFS.open(FPSTR(TEMP_FILE), "a");
  if(!receivedFile) {
    serialPort.printf_P(PSTR("%s Opening failed: %s\r\n"), FILE_TRANSFER_PREFIX, TEMP_FILE);
    receivedFile.close();
    return false;
  }
  const uint32_t writtenBytes = receivedFile.write(fileData, fileDataSize);
  receivedFile.close();
  if(writtenBytes != fileDataSize) {
    serialPort.printf_P(PSTR("%s Writing failed: %s\r\n"), FILE_TRANSFER_PREFIX, TEMP_FILE);
    return false;
  }
  nextFilePieceNumberLocal++;
  remainingFileSizeLocal -= fileDataSize;
  return true;
}

bool DataTransfer::checkValidity() {
  if(!isFileTransferStarted) { return false; }
  if(remainingFileSizeLocal != 0U) { return false; }

  File receivedFile = LittleFS.open(FPSTR(TEMP_FILE), "r");
  serialPort.printf_P(PSTR("%s Checking received file: %s\r\n"), FILE_TRANSFER_PREFIX, Str::getStateStr(receivedFile));
  if(!receivedFile) {
    receivedFile.close();
    return false;
  }
  const bool fileSizeOk = (receivedFile.size() == fileSizeLocal);
  serialPort.printf_P(PSTR("  Size -> %s\r\n"), Str::getStateStr(fileSizeOk));
  if(!fileSizeOk) {
    receivedFile.close();
    return false;
  }

  {
    Crc32 crc32;
    while(receivedFile.available() > 0) {
      crc32.next(receivedFile.read());
    }
    const uint32_t calcFileCrc32 = crc32.get();
    const bool fileCrcOk = (calcFileCrc32 == fileCrcLocal);
    serialPort.printf_P(PSTR("  CRC -> %s\r\n"), Str::getStateStr(fileCrcOk));
    if(!fileCrcOk) {
      receivedFile.close();
      return false;
    }
  }

  {
    const bool fileExists = LittleFS.exists(FPSTR(fileNameLocal));
    if(fileExists) {
      const bool rmFileResult = LittleFS.remove(FPSTR(fileNameLocal));
      if(!rmFileResult) {
        serialPort.printf_P(PSTR("%s Deleting failed: %s\r\n"), FILE_TRANSFER_PREFIX, fileNameLocal);
        receivedFile.close();
        return false;
      }
    }
    const bool renameResult = LittleFS.rename(FPSTR(TEMP_FILE), fileNameLocal);
    if(!renameResult) {
      serialPort.printf_P(PSTR("%s Renaming failed: %s -> %s\r\n"), FILE_TRANSFER_PREFIX, TEMP_FILE, fileNameLocal);
      receivedFile.close();
      return false;
    }
  }

  fileSizeLocal = 0U;
  fileCrcLocal = 0U;
  nextFilePieceNumberLocal = -1;
  remainingFileSizeLocal = 0U;
  isFileTransferStarted = false;
  receivedFile.close();
  return true;
}

bool DataTransfer::upgradeFirmware(const char* firmwareFileName) {
  if(firmwareFileName == nullptr) { return false; }

  // receivedFile.seek(0U, SeekSet);
  // const bool updateBeginResult = Update.begin(fileSizeLocal);
  // serialPort.printf_P(PSTR("  Begin -> %s\r\n"), Str::getStateStr(updateBeginResult));
  // if(!updateBeginResult) { receivedFile.close(); return false; }
  // const bool updateStreamResult = (Update.writeStream(receivedFile) == fileSizeLocal);
  // serialPort.printf_P(PSTR("  Stream -> %s\r\n"), Str::getStateStr(updateStreamResult));
  // if(!updateStreamResult) { receivedFile.close(); return false; }
  // receivedFile.close();
  // const bool updateEndResult = Update.end();
  // serialPort.printf_P(PSTR("  End -> %s\r\n"), Str::getStateStr(updateEndResult));
  // //stop(true);
  // if(!updateEndResult) { return false; }

  return true;
}