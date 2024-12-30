#ifndef DATA_TRANSFER_HPP
#define DATA_TRANSFER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <pgmspace.h>                                               /// Provides PROGMEM support for storing data in flash memory.
#include <HardwareSerial.h>                                         /// Hardware serial driver for communication with peripheral devices.

/// @brief Class to handle file transfer and firmware upgrade operations.
class DataTransfer final {
public:
  /// @brief Constructor.
  /// @param serial Reference to a HardwareSerial instance for communication.
  explicit DataTransfer(HardwareSerial& serial);

  /// @brief Destructor of the object.
  virtual ~DataTransfer() = default;

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

  /// @brief Stores a piece of the file as binary data.
  /// @param filePieceNumber The sequential piece number of the file being transferred.
  /// @param fileData A pointer to the binary data to store.
  /// @param fileDataSize The size of the binary data in bytes.
  /// @return True if the file piece is successfully stored, false otherwise.
  bool store(uint32_t filePieceNumber, const uint8_t* fileData, uint16_t fileDataSize);

  /// @brief Validates the received file.
  /// Checks the file size and CRC32 checksum to ensure the file was transferred correctly.
  /// @return True if the file is valid, false otherwise.
  bool checkValidity();

  /// @brief Initiates a firmware upgrade.
  /// @return True if the firmware upgrade process is successfully initiated, false otherwise.
  bool upgradeFirmware();

  DataTransfer(const DataTransfer&) = delete;                       // Define copy constructor.
  DataTransfer& operator=(const DataTransfer&) = delete;            // Define copy assignment operator.
  DataTransfer(DataTransfer&&) = delete;                            // Define move constructor.
  DataTransfer& operator=(DataTransfer&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Performs the firmware upgrade process.
  /// @param serial Reference to a HardwareSerial instance for logging.
  /// @param firmwareFileName The name of the firmware file.
  /// @return True if the firmware upgrade is successful, false otherwise.
  static bool upgradeFirmware(HardwareSerial& serial, const char* firmwareFileName);

  static constexpr uint8_t fileNameSize = 32U;                      // Maximum length of the file name.
  static constexpr uint16_t filePieceSize = 336U;                   // Size of a file piece. Divisible by both 3 and 4.
  static constexpr uint16_t maxB64Length = filePieceSize * 4U / 3U; // Maximum base64-encoded piece length.

  HardwareSerial& serialPort;                                       // Reference to the serial port for communication.
  uint32_t fileSizeLocal;                                           // Size of the file being transferred, in bytes.
  uint32_t fileCrcLocal;                                            // CRC32 checksum of the file.
  uint32_t nextFilePieceNumberLocal;                                // The next expected file piece number.
  uint32_t remainingFileSizeLocal;                                  // Remaining size of the file to be transferred.
  char fileNameLocal[fileNameSize];                                 // Buffer to store the file name.
  bool isFileTransferStarted;                                       // Indicates if a file transfer is in progress.
};
#endif // DATA_TRANSFER_HPP