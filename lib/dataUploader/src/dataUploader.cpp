#include "dataUploader.hpp"
#if defined(ESP8266) || defined(ESP32)  // ESP-only; empty translation unit elsewhere.
#include "base64.hpp"                                               /// Base64 encoding utilities.

DataUploader::DataUploader(void (*completeCb)(bool ok)) :
  completeCb(completeCb),
  queue{},
  queueHead(0U),
  queueCount(0U),
  current{},
  currentMd5{'\0'},
  offset(0U),
  pieceIndex(0U),
  uploadState(UploadState::IDLE),
  ackTimer(0U),
  sourceFile(),
  md5()
{}

DataUploader::~DataUploader() {
  releaseCurrent();
}

bool DataUploader::enqueue(const char* name, const uint8_t* data, uint32_t size, ReleaseCb release, void* ctx) {
  LockGuard guard(mutex);                                          // Producer task: serialize against the consumer.
  if((name == nullptr) || (strnlen(name, nameSize) == 0U)) {
    errState.setError(DataUploaderError::NAME_INVALID);
    return false;
  }
  if(size == 0U) {
    errState.setError(DataUploaderError::SIZE_ZERO);
    return false;
  }
  if(data == nullptr) {
    errState.setError(DataUploaderError::DATA_NULLPTR);
    return false;
  }
  UploadJob job{};
  strlcpy(job.name, name, sizeof(job.name));
  job.source = Source::RAM;
  job.data = data;
  job.size = size;
  job.release = release;
  job.releaseCtx = ctx;
  return pushJob(job);
}

bool DataUploader::enqueueFile(const char* name, const char* path) {
  LockGuard guard(mutex);                                          // Producer task: serialize against the consumer.
  if((name == nullptr) || (strnlen(name, nameSize) == 0U) ||
     (path == nullptr) || (strnlen(path, nameSize) == 0U)) {
    errState.setError(DataUploaderError::NAME_INVALID);
    return false;
  }
  UploadJob job{};
  strlcpy(job.name, name, sizeof(job.name));
  strlcpy(job.path, path, sizeof(job.path));
  job.source = Source::FILE;
  job.data = nullptr;
  job.size = 0U;                                  // Determined when the job is started (from the file size).
  job.release = nullptr;
  job.releaseCtx = nullptr;
  return pushJob(job);
}

bool DataUploader::pushJob(const UploadJob& job) {
  if(queueCount >= queueCapacity) {
    errState.setError(DataUploaderError::QUEUE_FULL);
    return false;
  }
  const uint8_t tail = static_cast<uint8_t>((queueHead + queueCount) % queueCapacity);
  queue[tail] = job;
  queueCount++;
  return true;
}

bool DataUploader::startNextJob() {
  if(queueCount == 0U) { return false; }
  current = queue[queueHead];
  queueHead = static_cast<uint8_t>((queueHead + 1U) % queueCapacity);
  queueCount--;
  offset = 0U;
  pieceIndex = 0U;
  memset(currentMd5, '\0', sizeof(currentMd5));

  if(current.source == Source::FILE) {
    sourceFile = LittleFS.open(current.path, "r");
    if(!sourceFile) {
      Logger::get().printf_P(PSTR("[UP] Opening file failed: %s\r\n"), current.path);
      errState.setError(DataUploaderError::FILE_OPEN_ERROR);
      return false;
    }
    current.size = sourceFile.size();
    if(current.size == 0U) {
      errState.setError(DataUploaderError::SIZE_ZERO);
      return false;
    }
  }
  return true;
}

bool DataUploader::computeMd5() {
  md5.begin();
  // MD5Builder::add() takes a uint16_t length on ESP platforms, so feed the source in chunks
  // (a camera JPEG easily exceeds 65535 bytes).
  if(current.source == Source::RAM) {
    uint32_t fed = 0U;
    while(fed < current.size) {
      const uint16_t toAdd = (current.size - fed >= rawChunkSize) ? rawChunkSize : static_cast<uint16_t>(current.size - fed);
      // MD5Builder::add takes a non-const pointer but does not modify the buffer.
      md5.add(const_cast<uint8_t*>(current.data + fed), toAdd);
      fed += toAdd;
    }
  } else {
    if(!sourceFile) {
      errState.setError(DataUploaderError::MD5_ERROR);
      return false;
    }
    sourceFile.seek(0U);
    uint8_t buffer[rawChunkSize] = {0U};
    uint32_t remaining = current.size;
    while(remaining > 0U) {
      const uint16_t toRead = (remaining >= rawChunkSize) ? rawChunkSize : static_cast<uint16_t>(remaining);
      const int32_t bytesRead = sourceFile.read(buffer, toRead);
      if(bytesRead != static_cast<int32_t>(toRead)) {
        errState.setError(DataUploaderError::MD5_ERROR);
        return false;
      }
      md5.add(buffer, toRead);
      remaining -= toRead;
    }
    sourceFile.seek(0U);
  }
  md5.calculate();
  strlcpy(currentMd5, md5.toString().c_str(), sizeof(currentMd5));
  return true;
}

bool DataUploader::readChunk(uint8_t* buffer, uint16_t& readLength) {
  const uint32_t remaining = current.size - offset;
  readLength = (remaining >= rawChunkSize) ? rawChunkSize : static_cast<uint16_t>(remaining);
  if(current.source == Source::RAM) {
    memcpy(buffer, current.data + offset, readLength);
    return true;
  }
  if(!sourceFile) {
    errState.setError(DataUploaderError::READ_ERROR);
    return false;
  }
  const int32_t bytesRead = sourceFile.read(buffer, readLength);
  if(bytesRead != static_cast<int32_t>(readLength)) {
    errState.setError(DataUploaderError::READ_ERROR);
    return false;
  }
  return true;
}

size_t DataUploader::prepareMessage(char* out, size_t outSize) {
  LockGuard guard(mutex);                                          // Consumer task.
  if((out == nullptr) || (outSize == 0U)) { return 0U; }

  switch(uploadState) {
    case UploadState::SEND_BEGIN: {
      const int32_t len = snprintf_P(out, outSize, PSTR(R"({"name":"%s","fileSize":%u,"md5":"%s"})"),
                                     current.name, current.size, currentMd5);
      if((len < 0) || (len >= static_cast<int32_t>(outSize))) { return 0U; }
      uploadState = UploadState::WAIT_BEGIN_ACK;
      ackTimer = millis();
      return static_cast<size_t>(len);
    }
    case UploadState::SEND_PIECE: {
      uint8_t raw[rawChunkSize] = {0U};
      uint16_t rawLength = 0U;
      if(!readChunk(raw, rawLength)) {
        uploadState = UploadState::CLEANUP;
        return 0U;
      }
      // Base64-encode the raw chunk, then wrap it in the piece JSON.
      uint8_t encoded[encodedChunkSize + 1U] = {0U};
      const uint32_t encodedLen = Base64::encodeBase64(raw, encoded, rawLength, sizeof(encoded));
      if(encodedLen == 0U) {
        errState.setError(DataUploaderError::ENCODE_ERROR);
        uploadState = UploadState::CLEANUP;
        return 0U;
      }
      const int32_t len = snprintf_P(out, outSize, PSTR(R"({"piece":%u,"data":"%s"})"),
                                     pieceIndex, reinterpret_cast<const char*>(encoded));
      if((len < 0) || (len >= static_cast<int32_t>(outSize))) {
        errState.setError(DataUploaderError::ENCODE_ERROR);
        uploadState = UploadState::CLEANUP;
        return 0U;
      }
      uploadState = UploadState::WAIT_PIECE_ACK;
      ackTimer = millis();
      return static_cast<size_t>(len);
    }
    default: {
      return 0U;                                  // Nothing to send in IDLE / WAIT_* / COMPUTE / FINALIZE / CLEANUP.
    }
  }
}

void DataUploader::notifyAck(bool ok) {
  LockGuard guard(mutex);                                          // Consumer task (called from the MQTT RX callback).
  if(!ok) {
    Logger::get().printf_P(PSTR("[UP] Server NACK for: %s\r\n"), current.name);
    errState.setError(DataUploaderError::SERVER_NACK);
    uploadState = UploadState::CLEANUP;
    return;
  }
  switch(uploadState) {
    case UploadState::WAIT_BEGIN_ACK: {
      uploadState = UploadState::SEND_PIECE;
    } break;
    case UploadState::WAIT_PIECE_ACK: {
      offset += (current.size - offset >= rawChunkSize) ? rawChunkSize : (current.size - offset);
      pieceIndex++;
      uploadState = (offset >= current.size) ? UploadState::FINALIZE : UploadState::SEND_PIECE;
    } break;
    default: {
      // Stray ACK outside a waiting state; ignore.
    } break;
  }
}

void DataUploader::run() {
  LockGuard guard(mutex);                                          // Consumer task.
  // Enforce the ACK timeout while waiting for the server.
  if((uploadState == UploadState::WAIT_BEGIN_ACK) || (uploadState == UploadState::WAIT_PIECE_ACK)) {
    if(Time::hasElapsed(millis(), ackTimer, ackTimeoutTime)) {
      Logger::get().printf_P(PSTR("[UP] ACK timeout for: %s\r\n"), current.name);
      errState.setError(DataUploaderError::ACK_TIMEOUT);
      uploadState = UploadState::CLEANUP;
    }
  }

  switch(uploadState) {
    case UploadState::IDLE: {
      if(queueCount > 0U) {
        uploadState = startNextJob() ? UploadState::COMPUTE : UploadState::CLEANUP;
      }
    } break;
    case UploadState::COMPUTE: {
      uploadState = computeMd5() ? UploadState::SEND_BEGIN : UploadState::CLEANUP;
      if(uploadState == UploadState::SEND_BEGIN) {
        Logger::get().printf_P(PSTR("[UP] Upload started:\r\n  Name: %s\r\n  Size: %u\r\n  MD5: %s\r\n"),
                               current.name, current.size, currentMd5);
      }
    } break;
    case UploadState::FINALIZE: {
      Logger::get().printf_P(PSTR("[UP] Upload finished: %s\r\n"), current.name);
      releaseCurrent();
      if(completeCb != nullptr) { completeCb(true); }
      uploadState = UploadState::IDLE;
    } break;
    case UploadState::CLEANUP: {
      releaseCurrent();
      if(completeCb != nullptr) { completeCb(false); }
      uploadState = UploadState::IDLE;
    } break;
    default: {
      // SEND_* / WAIT_* states are advanced by prepareMessage() and notifyAck().
    } break;
  }
}

void DataUploader::releaseCurrent() {
  if(sourceFile) {
    sourceFile.close();
  }
  if((current.source == Source::RAM) && (current.release != nullptr)) {
    current.release(current.releaseCtx);
    current.release = nullptr;                     // Guard against a double release.
  }
  current.data = nullptr;
}

DataUploader::DataUploaderErrorType DataUploader::getErrorCode() {
  LockGuard guard(mutex);                                          // Consumer task.
  const DataUploaderErrorType code = errState.getRawErrorState();
  errState.clearAllErrors();
  return code;
}

#endif  // defined(ESP8266) || defined(ESP32)
