#ifndef OTA_HPP
#define OTA_HPP

#include <Arduino.h>                          /// Arduino libraries header.

class OTA {

public:
  OTA(Stream* serial = nullptr);

  /// @brief Destructor of the object.
  virtual ~OTA() = default;

  bool begin(uint32_t fwSize, uint32_t fwCrc, const char* fileName = OTA_FW_LOCATION);

  bool store(uint32_t fwPieceNumber, const uint8_t* fwData, uint16_t fwDataSize);

  bool checkValidity();

  OTA(const OTA&) = delete;                       // Define copy constructor.
  OTA& operator=(const OTA&) = delete;            // Define copy assignment operator.
  OTA(OTA&&) = delete;                            // Define move constructor.
  OTA& operator=(OTA&&) = delete;                 // Define move assignment operator.

private:
  uint32_t fwSize;
  uint32_t fwCrc;
  Stream* serialPort;
  uint32_t nextFwPieceNumber;
  uint32_t remainingFwSize;
  const char* fileName_;

  static const char PROGMEM OTA_PREFIX[];
  static const char PROGMEM OTA_FW_LOCATION[];

};
#endif // OTA_HPP