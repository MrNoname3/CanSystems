#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "common.hpp"                                               /// Common definitions and functions.
#include <LittleFS.h>                                               /// Use FLASH filesystem.
#include <MD5Builder.h>                                             /// Utility for calculating MD5 checksums.

/// @brief Class to handle file transfer and firmware upgrade operations.
class DataTransfer final {
private:
  static constexpr uint8_t fileNameSize = 32U;                          // Maximum length of the file name.
  static constexpr uint8_t fileMd5Size = 33U;                           // MD5 hex string length (32 chars + null terminator).
  static constexpr uint32_t transferTimeoutTime = Time::minToMs(15U);   // Timeout period for the transfer process.
  static constexpr uint8_t readBufferSize = 64U;                        // Buffer size for reading file chunks.
  static constexpr uint32_t invalidFilePieceNumber = 0xFFFFFFFFU;       // Indicator for an invalid file piece number.
  // Piece-size cap: 126 is the closest multiple of 3 to 128, so a full piece encodes to whole
  // base64 groups with no padding. Comfortably above the tuned 100-byte sender pieces (larger
  // pieces measured slower, see ota/otaUpdate.py); the 1024-byte MQTT packet could carry 336.
  static constexpr uint16_t maxFilePieceLength = 126U + 1U;             // Maximum allowed length for a file piece.
  using DataTransferErrorType = uint32_t;                               // Type used for error codes.

  /// @brief Error codes for file transfer operations.
  enum class DataTransferError : DataTransferErrorType {
    // clang-format off
    NONE                      = 0U,                     // No error.
    FILE_SIZE_ZERO            = 1 << 0U,                // The provided file size is zero.
    FILE_MD5_NULLPTR          = 1 << 1U,                // The MD5 pointer is null.
    FILE_MD5_INVALID          = 1 << 2U,                // The MD5 string is invalid (e.g., empty).
    FILE_NAME_NULLPTR         = 1 << 3U,                // The file name pointer is null.
    FILE_NAME_INVALID         = 1 << 4U,                // The file name is invalid (e.g., empty).
    FILE_NAME_NOT_ALLOWED     = 1 << 5U,                // The file name is not in the list of accepted locations.
    NOT_ENOUGH_STORAGE        = 1 << 6U,                // Not enough storage space to store the file.
    BEGIN_NOT_CALLED          = 1 << 7U,                // The begin() method was not called prior to storing file pieces.
    WRONG_FILE_PIECE_NUMBER   = 1 << 8U,                // The file piece number is not sequential.
    FILE_ALREADY_STORED       = 1 << 9U,                // The file has already been fully stored.
    FILE_DATA_NULLPTR         = 1 << 10U,               // The provided file data pointer is null.
    B64_FILE_DATA_SIZE_ERROR  = 1 << 11U,               // Base64 file data size is not valid.
    FILE_PIECE_SIZE_OVEFLOW   = 1 << 12U,               // The decoded file piece size exceeds the maximum allowed.
    FILE_PIECE_SIZE_ERROR     = 1 << 13U,               // The file piece size is incorrect.
    B64_DECODED_SIZE_ERROR    = 1 << 14U,               // Error in the size after base64 decoding.
    TEMP_FILE_OPENING_ERROR   = 1 << 15U,               // Error opening the temporary file for writing.
    TEMP_FILE_WRITING_ERROR   = 1 << 16U,               // Error writing to the temporary file.
    RECEIVED_FILE_SIZE_ERROR  = 1 << 17U,               // The received file size does not match the expected size.
    FILE_MD5_ERROR            = 1 << 18U,               // MD5 checksum validation failed.
    TEMP_FILE_RENAMING_ERROR  = 1 << 19U,               // Error renaming the temporary file.
    FW_UPGRADE_BEGIN_FAILED   = 1 << 20U,               // Firmware upgrade initialization failed.
    FW_UPGRADE_SET_MD5_FAILED = 1 << 21U,               // Firmware upgrade MD5 configuration failed.
    FW_UPGRADE_WRITE_FAILED   = 1 << 22U,               // Firmware upgrade chunk write failed.
    FW_UPGRADE_END_FAILED     = 1 << 23U,               // Firmware upgrade finalization failed.
    RECEIVED_DATA_OVERRUN     = 1 << 24U                // More data received than the declared file size.
    // clang-format on
  };

public:
  /// @brief Represents the current state of the file transfer process.
  enum class TransferState : uint8_t {
    IDLE = 0U,                // No transfer is in progress.
    STORING,                  // Currently receiving and storing file pieces.
    CHECK,                    // Validating the received file (e.g., via MD5).
    CLEANUP                   // Cleaning up resources after the transfer or in case of an error.
  };

  /// @brief Constructor.
  /// @param checkOkCallback Callback function that is invoked when the transfer process
  ///        completes successfully (true) or fails (false).
  explicit DataTransfer(void (*checkOkCallback)(bool isValid));

  /// @brief Destructor. Closes any open file handles.
  ~DataTransfer();

  /// @brief Begins a new file transfer.
  /// @param fileSize The size of the file to be transferred in bytes.
  /// @param fileMd5 The MD5 checksum of the file as a hex string.
  /// @param fileName The name of the file to be transferred.
  /// @return True if the transfer is successfully initiated, false otherwise.
  [[nodiscard]] bool begin(uint32_t fileSize, const char* fileMd5, const char* fileName);

  /// @brief Stores a piece of the file, provided in base64 format.
  /// @param filePieceNumber The sequential piece number of the file being transferred.
  /// @param fileData The base64-encoded data to store.
  /// @return True if the file piece is successfully stored, false otherwise.
  [[nodiscard]] bool storeBase64(uint32_t filePieceNumber, const char* fileData);

  /// @brief Returns the name of the file currently being (or last) transferred.
  /// @return Pointer to the file name buffer.
  [[nodiscard]] const char* getFileName() const { return fileNameLocal; }

  /// @brief Retrieves the error code for the last file transfer operation.
  /// @return A DataTransferErrorType value representing the error code.
  [[nodiscard]] DataTransferErrorType getErrorCode();

  /// @brief Executes periodic validation tasks for non-firmware file transfers.
  /// Checks the file size and MD5 checksum to ensure the file was transferred correctly.
  void runValidityCheck();

  DataTransfer(const DataTransfer&) = delete;                       // Define copy constructor.
  DataTransfer& operator=(const DataTransfer&) = delete;            // Define copy assignment operator.
  DataTransfer(DataTransfer&&) = delete;                            // Define move constructor.
  DataTransfer& operator=(DataTransfer&&) = delete;                 // Define move assignment operator.

private:
  bool finalizeTransfer();                                          // Finalizes the transfer after all pieces are received.

  void (*checkOkCallback)(bool isValid);                            // Callback function invoked on transfer completion.
  uint32_t fileSizeLocal;                                           // Expected size of the file being transferred (in bytes).
  char fileMd5Local[fileMd5Size];                                   // Expected MD5 checksum of the file as a hex string.
  uint32_t nextFilePieceNumberLocal;                                // The next expected file piece number.
  uint32_t remainingFileSizeLocal;                                  // Remaining number of bytes to be received.
  char fileNameLocal[fileNameSize];                                 // Buffer storing the name of the file.
  bool isFwTransfer;                                                // Flag indicating whether the current transfer is a firmware upgrade.
  TransferState transferState;                                      // Current state of the file transfer process.
  ErrorState<DataTransferError, DataTransferErrorType> dataTransferErrState;  // Error state manager.
  uint32_t transferTimeoutTimer;                                    // Timer to track transfer timeout.
  File receivedFile;                                                // File object for the temporary storage of the transferred file.
  MD5Builder md5;                                                   // MD5 calculator used for non-firmware file integrity verification.
};