#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "common.hpp"                                               /// Common definitions and functions.
#include <LittleFS.h>                                               /// Use FLASH filesystem (file-sourced uploads).
#include <MD5Builder.h>                                             /// Utility for calculating MD5 checksums.
#include "sync.hpp"                                                 /// RecursiveMutex/LockGuard (no-op off-ESP32).

/// @brief Client -> server file upload engine: the mirror image of `DataTransfer`.
/// @details `DataTransfer` receives a file from the server in base64 pieces; this class sends one
/// the other way. It is deliberately transport-agnostic (it never touches MQTT) so it can be shared
/// between ESP32 and ESP8266 devices the same way `DataTransfer` is. The owning handler drives it:
///   1. `enqueue*()` queues an upload job (the "publicly accessible queue mechanism").
///   2. The handler calls `prepareMessage()` to obtain the next JSON payload and publishes it.
///   3. The handler forwards the server's ACK/NACK via `notifyAck()`.
///   4. `run()` is pumped periodically for timeouts and queue advancement.
///
/// Wire protocol (mirrors `DataTransfer`, but in the dtos -> stod direction):
///   begin:  {"name":"<file>","fileSize":<n>,"md5":"<hex>"}
///   piece:  {"piece":<idx>,"data":"<base64>"}
class DataUploader final {
public:
  /// @brief Where the bytes of an upload job live.
  enum class Source : uint8_t {
    RAM = 0U,                 // Borrowed buffer in (PS)RAM; released through `ReleaseCb` when the job ends.
    FILE                      // LittleFS file referenced by path.
  };

  /// @brief Callback invoked exactly once when a RAM-sourced job finishes (success or failure),
  /// so the producer can release the borrowed buffer (e.g. return a camera frame buffer).
  /// @param ctx Opaque context pointer supplied at enqueue time.
  using ReleaseCb = void (*)(void* ctx);

private:
  static constexpr uint8_t  nameSize        = 32U;                  // Maximum length of the logical upload name.
  static constexpr uint8_t  md5Size         = 33U;                  // MD5 hex string length (32 chars + null terminator).
  static constexpr uint8_t  queueCapacity   = 4U;                   // Maximum number of pending upload jobs.
  static constexpr uint16_t rawChunkSize    = 336U;                 // Raw bytes per piece (mirrors DataTransfer::maxFilePieceLength).
  static constexpr uint16_t encodedChunkSize = 4U * ((rawChunkSize + 2U) / 3U); // Base64 length of one raw chunk (no null).
  static constexpr uint32_t ackTimeoutTime  = Time::secToMs(30U);   // Time to wait for a server ACK before aborting.
  using DataUploaderErrorType = uint16_t;                           // Type used for error codes.

  /// @brief Error codes for upload operations.
  enum class DataUploaderError : DataUploaderErrorType {
    NONE                  = 0U,                  // No error.
    QUEUE_FULL            = 1 << 0U,             // No free slot in the upload queue.
    NAME_INVALID          = 1 << 1U,            // The provided name is null or empty.
    SIZE_ZERO             = 1 << 2U,             // The provided size is zero.
    DATA_NULLPTR          = 1 << 3U,            // RAM source buffer pointer is null.
    FILE_OPEN_ERROR       = 1 << 4U,            // The source file could not be opened.
    MD5_ERROR             = 1 << 5U,            // MD5 calculation failed.
    READ_ERROR            = 1 << 6U,            // Reading a chunk from the source failed.
    ENCODE_ERROR          = 1 << 7U,            // Base64 encoding of a chunk failed.
    ACK_TIMEOUT           = 1 << 8U,            // The server did not acknowledge in time.
    SERVER_NACK           = 1 << 9U             // The server rejected a message (NACK).
  };

public:
  /// @brief Current state of the upload state machine.
  enum class UploadState : uint8_t {
    IDLE = 0U,                // Nothing in progress; ready to pop the next job.
    COMPUTE,                  // Computing MD5 / opening source before the first send.
    SEND_BEGIN,               // A begin message is ready to be published.
    WAIT_BEGIN_ACK,           // Waiting for the server to ACK the begin message.
    SEND_PIECE,               // A piece message is ready to be published.
    WAIT_PIECE_ACK,           // Waiting for the server to ACK the last piece.
    FINALIZE,                 // All pieces sent and acknowledged; finishing the job.
    CLEANUP                   // Releasing resources after success or failure.
  };

  /// @brief Constructs an uploader.
  /// @param completeCb Callback invoked when a job finishes; `true` on success, `false` on failure.
  ///        May be `nullptr`.
  explicit DataUploader(void (*completeCb)(bool ok));

  /// @brief Destructor. Closes any open file handle and releases a borrowed RAM buffer.
  ~DataUploader();

  /// @brief Queues an upload from a borrowed (PS)RAM buffer.
  /// @param name Logical destination name reported to the server.
  /// @param data Pointer to the bytes to upload; must stay valid until `release` is invoked.
  /// @param size Number of bytes to upload.
  /// @param release Optional callback to free the buffer when the job ends.
  /// @param ctx Opaque context passed back to `release`.
  /// @return `true` if the job was queued, `false` otherwise (see `getErrorCode()`).
  [[nodiscard]] bool enqueue(const char* name, const uint8_t* data, uint32_t size,
                             ReleaseCb release = nullptr, void* ctx = nullptr);

  /// @brief Queues an upload sourced from a LittleFS file.
  /// @param name Logical destination name reported to the server.
  /// @param path LittleFS path of the file to upload.
  /// @return `true` if the job was queued, `false` otherwise (see `getErrorCode()`).
  [[nodiscard]] bool enqueueFile(const char* name, const char* path);

  /// @brief Builds the next MQTT JSON payload to publish.
  /// @param out Output buffer for the JSON payload.
  /// @param outSize Size of the output buffer.
  /// @return Length of the produced payload, or `0` if there is nothing to send right now
  ///         (idle, or waiting for an ACK).
  [[nodiscard]] size_t prepareMessage(char* out, size_t outSize);

  /// @brief Feeds the server's response for the last published message into the state machine.
  /// @param ok `true` for ACK, `false` for NACK.
  void notifyAck(bool ok);

  /// @brief Periodic pump: advances the queue and enforces the ACK timeout.
  void run();

  /// @brief Whether a job is currently being uploaded.
  [[nodiscard]] bool isBusy() const { return uploadState != UploadState::IDLE; }

  /// @brief Number of jobs waiting in the queue (excluding the one in progress).
  [[nodiscard]] uint8_t pending() const { return queueCount; }

  /// @brief Whether the queue has room for another job.
  [[nodiscard]] bool hasFreeSlot() const { return queueCount < queueCapacity; }

  /// @brief Returns the logical name of the job currently being uploaded.
  [[nodiscard]] const char* getCurrentName() const { return current.name; }

  /// @brief Retrieves and clears the accumulated error code.
  [[nodiscard]] DataUploaderErrorType getErrorCode();

  DataUploader(const DataUploader&) = delete;                       // Define copy constructor.
  DataUploader& operator=(const DataUploader&) = delete;            // Define copy assignment operator.
  DataUploader(DataUploader&&) = delete;                            // Define move constructor.
  DataUploader& operator=(DataUploader&&) = delete;                 // Define move assignment operator.

private:
  /// @brief A single queued upload job.
  struct UploadJob {
    char name[nameSize];                       // Logical destination name.
    char path[nameSize];                       // LittleFS path (FILE source only).
    Source source;                             // RAM or FILE.
    const uint8_t* data;                       // Source bytes (RAM source only).
    uint32_t size;                             // Total byte size.
    ReleaseCb release;                         // Buffer release callback (RAM source only).
    void* releaseCtx;                          // Context for `release`.
  };

  /// @brief Pops the next job from the queue into `current` and opens/measures the source.
  /// @return `true` if a job became current, `false` if the queue is empty or preparation failed.
  bool startNextJob();

  /// @brief Computes the MD5 of the current job's source into `current.md5`.
  /// @return `true` on success, `false` otherwise.
  bool computeMd5();

  /// @brief Reads up to `rawChunkSize` raw bytes at the current offset into `buffer`.
  /// @param buffer Destination for the raw bytes.
  /// @param[out] readLength Number of bytes actually read.
  /// @return `true` on success, `false` on read error.
  bool readChunk(uint8_t* buffer, uint16_t& readLength);

  /// @brief Releases the current job's resources (file handle, borrowed buffer).
  void releaseCurrent();

  /// @brief Enqueues a fully-populated job into the ring buffer.
  /// @return `true` if stored, `false` if the queue is full.
  bool pushJob(const UploadJob& job);

  void (*completeCb)(bool ok);                                      // Job-completion callback.
  UploadJob queue[queueCapacity];                                   // Ring buffer of pending jobs.
  uint8_t queueHead;                                                // Index of the next job to pop.
  uint8_t queueCount;                                               // Number of jobs currently queued.
  UploadJob current;                                                // Job currently being uploaded.
  char currentMd5[md5Size];                                         // MD5 hex string of the current job.
  uint32_t offset;                                                  // Byte offset of the next piece to send.
  uint32_t pieceIndex;                                              // Sequential index of the next piece.
  UploadState uploadState;                                          // Current state of the upload state machine.
  uint32_t ackTimer;                                                // Timestamp of the last sent message (for ACK timeout).
  File sourceFile;                                                  // Open file handle (FILE source only).
  MD5Builder md5;                                                   // MD5 calculator.
  ErrorState<DataUploaderError, DataUploaderErrorType> errState;    // Error state manager.
  RecursiveMutex mutex;                                            // Serializes the public API across producer (enqueue) and consumer (run) tasks; no-op off-ESP32.
};
