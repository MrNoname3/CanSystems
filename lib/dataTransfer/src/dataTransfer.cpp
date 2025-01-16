#include "dataTransfer.hpp"
#include <LittleFS.h>                                               /// Use FLASH filesystem.
#include "crc32.hpp"                                                /// Utility for calculating CRC32 checksums.
#include "base64.hpp"                                               /// Base64 encoding and decoding utilities.
#include "common.hpp"                                               /// Common definitions and functions.
#ifdef ESP8266
#include <Updater.h>                                                /// ESP8266-specific firmware update functionality.
#elif defined ESP32
#include <Update.h>                                                 /// ESP32-specific firmware update functionality.
#endif

DataTransfer::DataTransfer() :
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
  strncpy_P(fileNameLocal, fileName, sizeof(fileNameLocal) - 1U);
  fileNameLocal[sizeof(fileNameLocal) - 1U] = '\0';
  const uint32_t fileNameSize = strlen(fileNameLocal);
  if(fileNameSize == 0U) { return false; }
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
      Logger::get().printf_P(PSTR("[FT] Not enough free space!\r\n  Available: %u\r\n  Required: %u\r\n"), freeSpace, fileSizeLocal);
      return false;
    }
  }
  const bool fileExists = LittleFS.exists(FPSTR(FileName::getTempFileLocation()));
  if(fileExists) {
    const bool rmFileResult = LittleFS.remove(FPSTR(FileName::getTempFileLocation()));
    if(!rmFileResult) {
      Logger::get().printf_P(PSTR("[FT] Deleting failed: %s\r\n"), FileName::getTempFileLocation());
      return false;
    }
  }
  Logger::get().printf_P(PSTR("[FT] File transfer started:\r\n  Name: %s\r\n  Size: %u\r\n  CRC32: %u\r\n"), fileNameLocal, fileSizeLocal, fileCrcLocal);
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

  uint8_t decodedData[filePieceSize];
  const uint32_t decodedPreSize = Base64::decodedLength(reinterpret_cast<const uint8_t*>(fileData), filePieceB64Size);
  if(decodedPreSize > sizeof(decodedData)) {
    Logger::get().printf_P(PSTR("[FT] File piece size error!\r\n"));
    return false;
  }
  const uint32_t decodedPostSize = Base64::decodeBase64(reinterpret_cast<const uint8_t*>(fileData), decodedData, filePieceB64Size);
  if(decodedPreSize != decodedPostSize) {
    Logger::get().printf_P(PSTR("[FT] Decoded size check error!\r\n"));
    return false;
  }
  const bool storingResult = store(filePieceNumber, decodedData, decodedPreSize);
  if(!storingResult) {
    Logger::get().printf_P(PSTR("[FT] File storing failed!\r\n"));
  }
  return storingResult;
}

bool DataTransfer::store(uint32_t filePieceNumber, const uint8_t* fileData, uint16_t fileDataSize) {
  if(!isFileTransferStarted) { return false; }
  if(filePieceNumber != nextFilePieceNumberLocal) { return false; }
  if(fileDataSize == 0U) { return false; }
  if(remainingFileSizeLocal == 0U) { return false; }
  if(fileData == nullptr) { return false; }

  File receivedFile = LittleFS.open(FPSTR(FileName::getTempFileLocation()), "a");
  if(!receivedFile) {
    Logger::get().printf_P(PSTR("[FT] Opening failed: %s\r\n"), FileName::getTempFileLocation());
    return false;
  }
  const uint32_t writtenBytes = receivedFile.write(fileData, fileDataSize);
  receivedFile.close();
  if(writtenBytes != fileDataSize) {
    Logger::get().printf_P(PSTR("[FT] Writing failed: %s\r\n"), FileName::getTempFileLocation());
    return false;
  }
  nextFilePieceNumberLocal++;
  remainingFileSizeLocal -= fileDataSize;
  return true;
}

bool DataTransfer::checkValidity() {
  if(!isFileTransferStarted) { return false; }
  if(remainingFileSizeLocal != 0U) { return false; }

  File receivedFile = LittleFS.open(FPSTR(FileName::getTempFileLocation()), "r");
  Logger::get().printf_P(PSTR("[FT] Checking received file: %s\r\n"), Str::getStateStr(receivedFile));
  if(!receivedFile) {
    return false;
  }
  const bool fileSizeOk = (receivedFile.size() == fileSizeLocal);
  Logger::get().printf_P(PSTR("  Size -> %s\r\n"), Str::getStateStr(fileSizeOk));
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
    Logger::get().printf_P(PSTR("  CRC -> %s\r\n"), Str::getStateStr(fileCrcOk));
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
        Logger::get().printf_P(PSTR("[FT] Deleting failed: %s\r\n"), fileNameLocal);
        receivedFile.close();
        return false;
      }
    }
    const bool renameResult = LittleFS.rename(FPSTR(FileName::getTempFileLocation()), fileNameLocal);
    if(!renameResult) {
      Logger::get().printf_P(PSTR("[FT] Renaming failed: %s -> %s\r\n"), FileName::getTempFileLocation(), fileNameLocal);
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

bool DataTransfer::upgradeFirmware() {
  if(isFileTransferStarted) { return false; }
  return upgradeFirmware(FileName::getOtaFwLocation());
}

bool DataTransfer::upgradeFirmware(const char* firmwareFileName) {
  if(firmwareFileName == nullptr) { return false; }
  const uint32_t firmwareFileNameLength = strnlen_P(firmwareFileName, fileNameSize);
  if(firmwareFileNameLength == 0U || firmwareFileNameLength == fileNameSize) { return false; }
  {
    const bool fileExists = LittleFS.exists(FPSTR(firmwareFileName));
    if(!fileExists) {
      Logger::get().printf_P(PSTR("[FT] No firmware file exists!\r\n"));
      return false;
    }
  }

  File receivedFile = LittleFS.open(FPSTR(firmwareFileName), "r");
  Logger::get().printf_P(PSTR("[FT] Checking firmware file: %s\r\n"), Str::getStateStr(receivedFile));
  if(!receivedFile) {
    return false;
  }
  {
    const uint32_t fwFileSize = receivedFile.size();
    const bool fwSizeOk = (fwFileSize > 0U);
    Logger::get().printf_P(PSTR("  Size -> %s\r\n"), Str::getStateStr(fwSizeOk));
    if(!fwSizeOk) {
      receivedFile.close();
      return false;
    }

    const bool updateBeginResult = Update.begin(fwFileSize);
    Logger::get().printf_P(PSTR("  Begin -> %s\r\n"), Str::getStateStr(updateBeginResult));
    if(!updateBeginResult) {
      receivedFile.close();
      return false;
    }
    const bool updateStreamResult = (Update.writeStream(receivedFile) == fwFileSize);
    Logger::get().printf_P(PSTR("  Stream -> %s\r\n"), Str::getStateStr(updateStreamResult));
    if(!updateStreamResult) {
      receivedFile.close();
      return false;
    }
  }
  {
    const bool updateEndResult = Update.end();
    Logger::get().printf_P(PSTR("  End -> %s\r\n"), Str::getStateStr(updateEndResult));
    if(!updateEndResult) {
      receivedFile.close();
      return false;
    }
  }

  const bool rmFileResult = LittleFS.remove(FPSTR(firmwareFileName));
  if(!rmFileResult) {
    Logger::get().printf_P(PSTR("  Cleanup -> %s\r\n"), Str::getStateStr(rmFileResult));
    receivedFile.close();
    return false;
  }

  receivedFile.close();
  return true;
}