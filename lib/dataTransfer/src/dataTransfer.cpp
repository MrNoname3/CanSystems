#include "dataTransfer.hpp"
#include "base64.hpp"                                               /// Base64 encoding and decoding utilities.
#ifdef ESP8266
#include <Updater.h>                                                /// ESP8266-specific firmware update functionality.
#elif defined ESP32
#include <Update.h>                                                 /// ESP32-specific firmware update functionality.
#endif

DataTransfer::DataTransfer(void (*checkOkCallback)(bool isValid)) :
  checkOkCallback(checkOkCallback),
  fileSizeLocal(0U),
  fileMd5Local{'\0'},
  nextFilePieceNumberLocal(invalidFilePieceNumber),
  remainingFileSizeLocal(0U),
  fileNameLocal{'\0'},
  isFwTransfer(false),
  transferState(TransferState::IDLE),
  transferTimeoutTimer(0U),
  receivedFile(),
  md5()
{}

DataTransfer::~DataTransfer() {
  if(receivedFile) {
    receivedFile.close();
  }
}

bool DataTransfer::begin(uint32_t fileSize, const char* fileMd5, const char* fileName) {
  if(fileSize == 0U) {
    dataTransferErrState.setError(DataTransferError::FILE_SIZE_ZERO);
    return false;
  }
  if(fileMd5 == nullptr) {
    dataTransferErrState.setError(DataTransferError::FILE_MD5_NULLPTR);
    return false;
  }
  if(strlen(fileMd5) == 0U) {
    dataTransferErrState.setError(DataTransferError::FILE_MD5_INVALID);
    return false;
  }
  if(fileName == nullptr) {
    dataTransferErrState.setError(DataTransferError::FILE_NAME_NULLPTR);
    return false;
  }
  if(strlen(fileName) == 0U) {
    dataTransferErrState.setError(DataTransferError::FILE_NAME_INVALID);
    return false;
  }
  if(!FileName::isValidFileName(fileName)) {
    dataTransferErrState.setError(DataTransferError::FILE_NAME_NOT_ALLOWED);
    return false;
  }
  fileSizeLocal = fileSize;
  memset(fileMd5Local, '\0', sizeof(fileMd5Local));
  strncpy(fileMd5Local, fileMd5, sizeof(fileMd5Local) - 1U);
  fileMd5Local[sizeof(fileMd5Local) - 1U] = '\0';
  nextFilePieceNumberLocal = 0U;
  remainingFileSizeLocal = fileSize;
  memset(fileNameLocal, '\0', sizeof(fileNameLocal));
  strncpy_P(fileNameLocal, fileName, sizeof(fileNameLocal) - 1U);
  fileNameLocal[sizeof(fileNameLocal) - 1U] = '\0';
  if(receivedFile) {
    receivedFile.close();
  }
  isFwTransfer = (strncmp_P(fileNameLocal, FileName::getOtaFwLocation(), sizeof(fileNameLocal)) == 0);
  if(isFwTransfer) {
    Update.end(false);
    const bool updateBeginResult = Update.begin(fileSizeLocal);
    Logger::get().printf_P(PSTR("[FT] Firmware update begin -> %s\r\n"), Str::getStateStr(updateBeginResult));
    if(!updateBeginResult) {
      dataTransferErrState.setError(DataTransferError::FW_UPGRADE_BEGIN_FAILED);
      return false;
    }
    if(!Update.setMD5(fileMd5Local)) {
      Logger::get().printf_P(PSTR("[FT] Failed to set MD5 for firmware update!\r\n"));
      dataTransferErrState.setError(DataTransferError::FW_UPGRADE_SET_MD5_FAILED);
      Update.end(false);
      return false;
    }
  } else {
    LittleFS.remove(FPSTR(FileName::getTempFileLocation()));
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
      dataTransferErrState.setError(DataTransferError::NOT_ENOUGH_STORAGE);
      return false;
    }
    receivedFile = LittleFS.open(FPSTR(FileName::getTempFileLocation()), "a");
    if(!receivedFile) {
      Logger::get().printf_P(PSTR("[FT] Opening failed for write: %s\r\n"), FileName::getTempFileLocation());
      dataTransferErrState.setError(DataTransferError::TEMP_FILE_OPENING_ERROR);
      return false;
    }
  }
  Logger::get().printf_P(PSTR("[FT] File transfer started:\r\n  Name: %s\r\n  Size: %u\r\n  MD5: %s\r\n"), fileNameLocal, fileSizeLocal, fileMd5Local);
  transferTimeoutTimer = millis();
  transferState = TransferState::STORING;
  return true;
}

bool DataTransfer::storeBase64(uint32_t filePieceNumber, const char* fileData) {
  if(transferState != TransferState::STORING) {
    dataTransferErrState.setError(DataTransferError::BEGIN_NOT_CALLED);
    return false;
  }
  if(filePieceNumber != nextFilePieceNumberLocal) {
    dataTransferErrState.setError(DataTransferError::WRONG_FILE_PIECE_NUMBER);
    return false;
  }
  if(remainingFileSizeLocal == 0U) {
    dataTransferErrState.setError(DataTransferError::FILE_ALREADY_STORED);
    return false;
  }
  if(fileData == nullptr) {
    dataTransferErrState.setError(DataTransferError::FILE_DATA_NULLPTR);
    return false;
  }
  const uint32_t filePieceB64Size = strlen(fileData);
  if((filePieceB64Size == 0U) || (filePieceB64Size % 4U != 0U)) {
    dataTransferErrState.setError(DataTransferError::B64_FILE_DATA_SIZE_ERROR);
    return false;
  }
  const uint16_t filePieceSize = filePieceB64Size * 3U / 4U + 1U;
  if(filePieceSize > maxFilePieceLength) {
    dataTransferErrState.setError(DataTransferError::FILE_PIECE_SIZE_OVEFLOW);
    return false;
  }
  uint8_t decodedData[filePieceSize];
  const uint32_t decodedPreSize = Base64::decodedLength(reinterpret_cast<const uint8_t*>(fileData), filePieceB64Size);
  if(decodedPreSize > filePieceSize || decodedPreSize == 0U) {
    Logger::get().printf_P(PSTR("[FT] File piece size error!\r\n"));
    dataTransferErrState.setError(DataTransferError::FILE_PIECE_SIZE_ERROR);
    return false;
  }
  const uint32_t decodedPostSize = Base64::decodeBase64(reinterpret_cast<const uint8_t*>(fileData), decodedData, filePieceB64Size, filePieceSize);
  if(decodedPreSize != decodedPostSize) {
    Logger::get().printf_P(PSTR("[FT] Decoded size check error!\r\n"));
    dataTransferErrState.setError(DataTransferError::B64_DECODED_SIZE_ERROR);
    return false;
  }
  if(isFwTransfer) {
    const uint32_t writtenBytes = Update.write(decodedData, decodedPostSize);
    if(writtenBytes != decodedPostSize) {
      Logger::get().printf_P(PSTR("[FT] Firmware write failed!\r\n"));
      dataTransferErrState.setError(DataTransferError::FW_UPGRADE_WRITE_FAILED);
      Update.end(false);
      transferState = TransferState::CLEANUP;
      return false;
    }
  } else {
    const uint32_t writtenBytes = receivedFile.write(decodedData, decodedPostSize);
    if(writtenBytes != decodedPostSize) {
      Logger::get().printf_P(PSTR("[FT] Writing failed: %s\r\n"), FileName::getTempFileLocation());
      dataTransferErrState.setError(DataTransferError::TEMP_FILE_WRITING_ERROR);
      return false;
    }
  }
  nextFilePieceNumberLocal++;
  remainingFileSizeLocal -= decodedPostSize;
  if(remainingFileSizeLocal == 0U) {
    return finalizeTransfer();
  }
  return true;
}

bool DataTransfer::finalizeTransfer() {
  if(isFwTransfer) {
    const bool updateEndResult = Update.end();
    Logger::get().printf_P(PSTR("[FT] Firmware update end -> %s\r\n"), Str::getStateStr(updateEndResult));
    if(!updateEndResult) {
      dataTransferErrState.setError(DataTransferError::FW_UPGRADE_END_FAILED);
      transferState = TransferState::CLEANUP;
      return false;
    }
    Logger::get().printf_P(PSTR("[FT] Firmware received and verified: %s\r\n"), fileNameLocal);
    transferState = TransferState::CLEANUP;
    if(checkOkCallback != nullptr) {
      checkOkCallback(true);
    }
  } else {
    receivedFile.close();
    receivedFile = LittleFS.open(FPSTR(FileName::getTempFileLocation()), "r");
    if(!receivedFile) {
      Logger::get().printf_P(PSTR("[FT] Opening file for read failed: %s\r\n"), FileName::getTempFileLocation());
      dataTransferErrState.setError(DataTransferError::TEMP_FILE_OPENING_ERROR);
      return false;
    }
    if(receivedFile.size() != fileSizeLocal) {
      Logger::get().printf_P(PSTR("[FT] File size mismatch! %u != %u\r\n"), receivedFile.size(), fileSizeLocal);
      dataTransferErrState.setError(DataTransferError::RECEIVED_FILE_SIZE_ERROR);
      return false;
    }
    md5.begin();
    transferState = TransferState::CHECK;
    transferTimeoutTimer = millis();
  }
  return true;
}

void DataTransfer::runValidityCheck() {
  const uint32_t actualTime = millis();
  if(Time::hasElapsed(actualTime, transferTimeoutTimer, transferTimeoutTime)) {
    transferState = TransferState::CLEANUP;
  }
  switch(transferState) {
    case TransferState::IDLE: {
      transferTimeoutTimer = actualTime;
    } break;
    case TransferState::STORING: {} break;
    case TransferState::CHECK: {
      const uint32_t remainingBytes = receivedFile.available();
      if(remainingBytes > 0U) {
        uint8_t readBuffer[readBufferSize] = {0U};
        const uint8_t readLength = (remainingBytes >= readBufferSize) ? readBufferSize : remainingBytes;
        receivedFile.read(readBuffer, readLength);
        md5.add(readBuffer, readLength);
      } else {
        transferState = TransferState::CLEANUP;
        md5.calculate();
        const bool fileMd5Ok = (strncasecmp(md5.toString().c_str(), fileMd5Local, sizeof(fileMd5Local)) == 0);
        if(!fileMd5Ok) {
          Logger::get().printf_P(PSTR("[FT] File MD5 mismatch! %s != %s\r\n"), md5.toString().c_str(), fileMd5Local);
          dataTransferErrState.setError(DataTransferError::FILE_MD5_ERROR);
          break;
        }
        if(receivedFile) {
          receivedFile.close();
        }
        LittleFS.remove(FPSTR(fileNameLocal));
        if(!LittleFS.rename(FPSTR(FileName::getTempFileLocation()), fileNameLocal)) {
          Logger::get().printf_P(PSTR("[FT] Renaming failed: %s -> %s\r\n"), FileName::getTempFileLocation(), fileNameLocal);
          dataTransferErrState.setError(DataTransferError::TEMP_FILE_RENAMING_ERROR);
          break;
        }
        Logger::get().printf_P(PSTR("[FT] File received: %s\r\n"), fileNameLocal);
        if(checkOkCallback != nullptr) {
          checkOkCallback(true);
        }
      }
    } break;
    case TransferState::CLEANUP: {
      fileSizeLocal = 0U;
      memset(fileMd5Local, '\0', sizeof(fileMd5Local));
      nextFilePieceNumberLocal = invalidFilePieceNumber;
      remainingFileSizeLocal = 0U;
      isFwTransfer = false;
      if(receivedFile) {
        receivedFile.close();
      }
      transferState = TransferState::IDLE;
      if((checkOkCallback != nullptr) && (dataTransferErrState.getRawErrorState() > 0U)) {
        checkOkCallback(false);
      }
    } break;
  }
}

DataTransfer::DataTransferErrorType DataTransfer::getErrorCode() { // NOLINT(readability-convert-member-functions-to-static)
  const DataTransferErrorType errCode = dataTransferErrState.getRawErrorState();
  dataTransferErrState.clearAllErrors();
  return errCode;
}