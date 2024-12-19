#ifndef DATA_TRANSFER_HPP
#define DATA_TRANSFER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <pgmspace.h>                                               /// Provides PROGMEM support for storing data in flash memory.
#include <HardwareSerial.h>                                         /// Hardware serial driver for communication with peripheral devices.

class DataTransfer final {
public:
  explicit DataTransfer(HardwareSerial& serial);

  /// @brief Destructor of the object.
  virtual ~DataTransfer() = default;

  bool begin(uint32_t fileSize, uint32_t fileCrc, const char* fileName);

  bool storeBase64(uint32_t filePieceNumber, const char* fileData);

  bool store(uint32_t filePieceNumber, const uint8_t* fileData, uint16_t fileDataSize);

  bool checkValidity();

  bool upgradeFirmware(const char* firmwareFileName);

  DataTransfer(const DataTransfer&) = delete;                       // Define copy constructor.
  DataTransfer& operator=(const DataTransfer&) = delete;            // Define copy assignment operator.
  DataTransfer(DataTransfer&&) = delete;                            // Define move constructor.
  DataTransfer& operator=(DataTransfer&&) = delete;                 // Define move assignment operator.

private:
  static bool upgradeFirmware(HardwareSerial& serial, const char* firmwareFileName);

  static constexpr uint8_t fileNameSize = 32U;                      // Maximum length of the file name.
  static constexpr uint16_t filePieceSize = 336U;                   // It should always be divisible by both 3 and 4!
  static constexpr uint16_t maxB64Length = filePieceSize * 4U / 3U;
  static const char PROGMEM FILE_TRANSFER_PREFIX[];
  static const char PROGMEM TEMP_FILE[];

  HardwareSerial& serialPort;
  uint32_t fileSizeLocal;
  uint32_t fileCrcLocal;
  uint32_t nextFilePieceNumberLocal;
  uint32_t remainingFileSizeLocal;
  char fileNameLocal[fileNameSize];
  bool isFileTransferStarted;
};
#endif // DATA_TRANSFER_HPP