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
  fileCrcLocal(0U),
  nextFilePieceNumberLocal(-1),
  remainingFileSizeLocal(0U),
  fileNameLocal{'\0'},
  transferState(TransferState::IDLE),
  transferTimeoutTimer(0U),
  receivedFile(),
  crc32()
{}

DataTransfer::~DataTransfer() {
  if(receivedFile) {
    receivedFile.close();
  }
}

bool DataTransfer::begin(uint32_t fileSize, uint32_t fileCrc, const char* fileName) {
  if(fileSize == 0U) {
    dataTransferErrState.setError(DataTransferError::FILE_SIZE_ZERO);
    return false;
  }
  fileSizeLocal = fileSize;
  fileCrcLocal = fileCrc;
  nextFilePieceNumberLocal = 0U;
  remainingFileSizeLocal = fileSize;
  if(fileName == nullptr) {
    dataTransferErrState.setError(DataTransferError::FILE_NAME_NULLPTR);
    return false;
  }
  memset(fileNameLocal, '\0', sizeof(fileNameLocal));
  strncpy_P(fileNameLocal, fileName, sizeof(fileNameLocal) - 1U);
  fileNameLocal[sizeof(fileNameLocal) - 1U] = '\0';
  const uint32_t fileNameSize = strlen(fileNameLocal);
  if(fileNameSize == 0U) {
    dataTransferErrState.setError(DataTransferError::FILE_NAME_INVALID);
    return false;
  }
  if(receivedFile) {
    receivedFile.close();
  }
  LittleFS.remove(FPSTR(FileName::getTempFileLocation()));
  LittleFS.remove(FPSTR(FileName::getOtaFwLocation()));
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
      dataTransferErrState.setError(DataTransferError::NOT_ENOUGH_STORAGE);
      return false;
    }
  }
  receivedFile = LittleFS.open(FPSTR(FileName::getTempFileLocation()), "a");
  if(!receivedFile) {
    Logger::get().printf_P(PSTR("[FT] Opening failed for write: %s\r\n"), FileName::getTempFileLocation());
    dataTransferErrState.setError(DataTransferError::TEMP_FILE_OPENING_ERROR);
    return false;
  }
  Logger::get().printf_P(PSTR("[FT] File transfer started:\r\n  Name: %s\r\n  Size: %u\r\n  CRC32: %u\r\n"), fileNameLocal, fileSizeLocal, fileCrcLocal);
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
  uint8_t decodedData[filePieceSize];
  const uint32_t decodedPreSize = Base64::decodedLength(reinterpret_cast<const uint8_t*>(fileData), filePieceB64Size);
  if(decodedPreSize > filePieceSize || decodedPreSize == 0U) {
    Logger::get().printf_P(PSTR("[FT] File piece size error!\r\n"));
    dataTransferErrState.setError(DataTransferError::FILE_PIECE_SIZE_ERROR);
    return false;
  }
  const uint32_t decodedPostSize = Base64::decodeBase64(reinterpret_cast<const uint8_t*>(fileData), decodedData, filePieceB64Size, filePieceSize);
  if(decodedPreSize != decodedPostSize || decodedPostSize == 0U) {
    Logger::get().printf_P(PSTR("[FT] Decoded size check error!\r\n"));
    dataTransferErrState.setError(DataTransferError::B64_DECODED_SIZE_ERROR);
    return false;
  }
  const uint32_t writtenBytes = receivedFile.write(decodedData, decodedPostSize);
  if(writtenBytes != decodedPostSize) {
    Logger::get().printf_P(PSTR("[FT] Writing failed: %s\r\n"), FileName::getTempFileLocation());
    dataTransferErrState.setError(DataTransferError::TEMP_FILE_WRITING_ERROR);
    return false;
  }
  nextFilePieceNumberLocal++;
  remainingFileSizeLocal -= decodedPostSize;
  if(remainingFileSizeLocal == 0U) {
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
    crc32.reset();
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
        crc32.next(readBuffer, readLength);
      } else {
        transferState = TransferState::CLEANUP;
        const bool fileCrcOk = (crc32.get() == fileCrcLocal);
        if(!fileCrcOk) {
          Logger::get().printf_P(PSTR("[FT] File CRC mismatch! %u != %u\r\n"), crc32.get(), fileCrcLocal);
          dataTransferErrState.setError(DataTransferError::FILE_CRC_ERROR);
          break;
        } else {
          if(receivedFile) {
            receivedFile.close();
          }
          LittleFS.remove(FPSTR(fileNameLocal));
          if(!LittleFS.rename(FPSTR(FileName::getTempFileLocation()), fileNameLocal)) {
            Logger::get().printf_P(PSTR("[FT] Renaming failed: %s -> %s\r\n"), FileName::getTempFileLocation(), fileNameLocal);
            dataTransferErrState.setError(DataTransferError::TEMP_FILE_RENAMING_ERROR);
            break;
          }
        }
        Logger::get().printf_P(PSTR("[FT] File received: %s\r\n"), fileNameLocal);
        if(strncmp_P(fileNameLocal, FileName::getOtaFwLocation(), sizeof(fileNameLocal)) == 0) {
          transferState = TransferState::UPGRADE_FW;
        } else {
          if(checkOkCallback != nullptr) {
            checkOkCallback(true);
          }
        }
      }
    } break;
    case TransferState::UPGRADE_FW: {
      transferState = TransferState::CLEANUP;
      receivedFile = LittleFS.open(FPSTR(FileName::getOtaFwLocation()), "r");
      if(!receivedFile) {
        Logger::get().printf_P(PSTR("[FT] Opening FW file for read failed: %s\r\n"), FileName::getOtaFwLocation());
        dataTransferErrState.setError(DataTransferError::FW_FILE_OPENING_ERROR);
        break;
      }
      const uint32_t fwFileSize = receivedFile.size();
      Logger::get().printf_P(PSTR("[FT] Checking firmware file:\r\n"));
      const bool updateBeginResult = Update.begin(fwFileSize);
      Logger::get().printf_P(PSTR("  Begin -> %s\r\n"), Str::getStateStr(updateBeginResult));
      if(!updateBeginResult) {
        dataTransferErrState.setError(DataTransferError::FW_UPGRADE_BEGIN_FAILED);
        break;
      }
      const bool updateStreamResult = (Update.writeStream(receivedFile) == fwFileSize);
      Logger::get().printf_P(PSTR("  Stream -> %s\r\n"), Str::getStateStr(updateStreamResult));
      if(!updateStreamResult) {
        dataTransferErrState.setError(DataTransferError::FW_UPGRADE_STREAM_FAILED);
        break;
      }
      const bool updateEndResult = Update.end();
      Logger::get().printf_P(PSTR("  End -> %s\r\n"), Str::getStateStr(updateEndResult));
      if(!updateEndResult) {
        dataTransferErrState.setError(DataTransferError::FW_UPGRADE_END_FAILED);
        break;
      }
      receivedFile.close();
      LittleFS.remove(FPSTR(FileName::getOtaFwLocation()));
      if(checkOkCallback != nullptr) {
        checkOkCallback(true);
      }
    } break;
    case TransferState::CLEANUP: {
      fileSizeLocal = 0U;
      fileCrcLocal = 0U;
      nextFilePieceNumberLocal = -1;
      remainingFileSizeLocal = 0U;
      crc32.reset();
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

DataTransfer::DataTransferErrorType DataTransfer::getErrorCode() {
  const DataTransferErrorType errCode = dataTransferErrState.getRawErrorState();
  dataTransferErrState.clearAllErrors();
  return errCode;
}