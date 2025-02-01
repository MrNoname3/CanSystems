#ifndef DATA_TRANSFER_HPP
#define DATA_TRANSFER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "common.hpp"                                               /// Common definitions and functions.
#include <LittleFS.h>                                               /// Use FLASH filesystem.
#include "crc32.hpp"                                                /// Utility for calculating CRC32 checksums.

/// @brief Class to handle file transfer and firmware upgrade operations.
class DataTransfer final {
private:
  static constexpr uint8_t fileNameSize = 32U;                          // Maximum length of the file name.
  static constexpr uint16_t filePieceSize = 336U;                       // Size of a file piece. Divisible by both 3 and 4.
  static constexpr uint16_t maxB64Length = filePieceSize * 4U / 3U;     // Maximum base64-encoded piece length.
  static constexpr uint32_t transferTimeoutTime = Time::minToMs(15U);
  static constexpr uint8_t readBufferSize = 64U;                        // Buffer size for reading file chunks.
  using DataTransferErrorType = uint32_t;

  enum class DataTransferError : DataTransferErrorType {
    NONE                      = 0U,                     // No error.
    FILE_SIZE_ZERO            = 1 << 0U,
    FILE_NAME_NULLPTR         = 1 << 1U,
    FILE_NAME_INVALID         = 1 << 2U,
    TEMP_FILE_REMOVAL_ERROR   = 1 << 3U,
    NOT_ENOUGH_STORAGE        = 1 << 4U,
    BEGIN_NOT_CALLED          = 1 << 5U,
    WRONG_FILE_PIECE_NUMBER   = 1 << 6U,
    FILE_ALREADY_STORED       = 1 << 7U,
    FILE_DATA_NULLPTR         = 1 << 8U,
    B64_FILE_DATA_EMPTY       = 1 << 9U,
    FILE_PIECE_SIZE_ERROR     = 1 << 10U,
    B64_DECODED_SIZE_ERROR    = 1 << 11U,
    TEMP_FILE_OPENING_ERROR   = 1 << 12U,
    TEMP_FILE_WRITING_ERROR   = 1 << 13U,
    RECEIVED_FILE_SIZE_ERROR  = 1 << 14U,
    FILE_CRC_ERROR            = 1 << 15U,
    FINAL_FILE_REMOVAL_ERROR  = 1 << 16U,
    TEMP_FILE_RENAMING_ERROR  = 1 << 17U,
    FW_FILE_OPENING_ERROR     = 1 << 18U,
    FW_UPGRADE_BEGIN_FAILED   = 1 << 19U,
    FW_UPGRADE_STREAM_FAILED  = 1 << 20U,
    FW_UPGRADE_END_FAILED     = 1 << 21U,
    FW_FILE_REMOVAL_ERROR     = 1 << 22U
  };

public:
  enum class TransferState : uint8_t {
    IDLE = 0U,
    STORING,
    CHECK,
    UPGRADE_FW,
    CLEANUP
  };

  /// @brief Constructor.
  explicit DataTransfer(void (*checkOkCallback)(bool isValid));

  /// @brief Destructor of the object.
  ~DataTransfer();

  /// @brief Begins a new file transfer.
  /// @param fileSize The size of the file to be transferred in bytes.
  /// @param fileCrc The CRC32 checksum of the file.
  /// @param fileName The name of the file to be transferred.
  /// @return True if the transfer is successfully initiated, false otherwise.
  bool begin(uint32_t fileSize, uint32_t fileCrc, const char* fileName);

  /// @brief Stores a piece of the file, provided in base64 format.
  /// @param filePieceNumber The sequential piece number of the file being transferred.
  /// @param fileData The base64-encoded data to store.
  /// @return True if the file piece is successfully stored, false otherwise.
  bool storeBase64(uint32_t filePieceNumber, const char* fileData);

  inline bool isTransferInProgress() { return (transferState != TransferState::IDLE); }

  inline DataTransferErrorType getErrorCode() {
    const DataTransferErrorType errCode = dataTransferErrState.getRawErrorState();
    dataTransferErrState.clearAllErrors();
    return errCode;
  }

  /// @brief Validates the received file.
  /// Checks the file size and CRC32 checksum to ensure the file was transferred correctly.
  /// @return True if the file is valid, false otherwise.
  void runValidityCheck();

  DataTransfer(const DataTransfer&) = delete;                       // Define copy constructor.
  DataTransfer& operator=(const DataTransfer&) = delete;            // Define copy assignment operator.
  DataTransfer(DataTransfer&&) = delete;                            // Define move constructor.
  DataTransfer& operator=(DataTransfer&&) = delete;                 // Define move assignment operator.

private:
  void (*checkOkCallback)(bool isValid);
  uint32_t fileSizeLocal;                                           // Size of the file being transferred, in bytes.
  uint32_t fileCrcLocal;                                            // CRC32 checksum of the file.
  uint32_t nextFilePieceNumberLocal;                                // The next expected file piece number.
  uint32_t remainingFileSizeLocal;                                  // Remaining size of the file to be transferred.
  char fileNameLocal[fileNameSize];                                 // Buffer to store the file name.
  TransferState transferState;
  ErrorState<DataTransferError, DataTransferErrorType> dataTransferErrState;
  uint32_t transferTimeoutTimer;
  File receivedFile;                                                // File object for the firmware being transferred.
  Crc32 crc32;
};
#endif // DATA_TRANSFER_HPP