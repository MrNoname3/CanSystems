#ifndef RFDRIVER_HPP
#define RFDRIVER_HPP

#include <Arduino.h>                          /// Arduino libraries header.
#include <SoftwareSerial.h>                   /// Arduino software serial lib.
#include "CircularBuffer.hpp"                 /// Circular buffer class.

/// @brief Driver class for the serially connected arduino,
/// which handles the RF transmitter and receiver modules.
class RFDriver {

public:

  /// @brief RF data transciev struct.
  struct RFData {
    char beginFrame = '*';                    // Serial sync character.
    uint32_t data = 0;                        // RF data.
    uint8_t bitLength = 0;                    // RF data bit length.
    uint8_t protocol = 0;                     // RF protocol.
    uint16_t pulseLength = 0;                 // RF pulse length.
    uint16_t crc = 0;                         // Data struct CRC value.
    char endFrame = '#';                      // Serial sync character.
  };

  /// @brief Constructor for RF driver class.
  /// @param RXpin Software serial RX pin.
  /// @param TXpin Software serial TX pin.
  RFDriver(uint8_t RXpin, uint8_t TXpin) : swSerial(RXpin, TXpin) {
    swSerial.begin(38400);                    // Open software serial port.
  }

  /// @brief Destructor of the object.
  virtual ~RFDriver() = default;

  /// @brief Put RF data struct to TX queue.
  /// @param rfData RF data structure.
  void sendRfData(const RFData &rfData) {
    serialTxbuffer.put(rfData);
  }

  /// @brief Get received RF data structure, if available.
  /// @param rfData RF data structure.
  /// @return Returns true if the data is valid.
  bool getRfData(RFData &rfData) {
    if(!serialRxbuffer.isEmpty()) {           // Check received data queue.
      rfData = serialRxbuffer.pop();          // If data available, get it.
      return true;                            // Return true -> data valid.
    }
    return false;                             // Return false -> data invalid.
  }

  /// @brief Need to run periodically to handle serial queues.
  /// @return Returns true if the data is valid.
  bool spin() {
    if(!serialTxbuffer.isEmpty()) {                                             // Check RF data TX buffer.
      RFData rfData = serialTxbuffer.pop();                                     // If data available, get it.
      rfData.crc = calCrc(reinterpret_cast<const uint8_t*>(&rfData), sizeof(RFData)); // Add CRC to data struct.
      swSerial.write(reinterpret_cast<uint8_t*>(&rfData), sizeof(RFData));      // Send data.
    }

    if(swSerial.available() >= (int16_t)sizeof(RFData)) {                       // Check if serial data arrived.
      RFData rfData;
      swSerial.readBytes(reinterpret_cast<uint8_t*>(&rfData), sizeof(RFData));  // Read arrived bytes to struct.
      if((rfData.beginFrame == '*') && (rfData.endFrame == '#')) {              // Check if frame begin and end are ok.
        uint16_t receivedCrc = rfData.crc;                                      // Save received CRC value.
        rfData.crc = 0;                                                         // Clear received CRC.
        uint16_t calculatedCrc = calCrc(reinterpret_cast<const uint8_t*>(&rfData), sizeof(RFData)); // Calculate CRC.
        if(receivedCrc == calculatedCrc) {
          serialRxbuffer.put(rfData);                                           // If yes, put the RF data struct to queue.
        }
      }
      else {                                                                    // If no, there is garbage in serial RX buffer.
        while(swSerial.available() > 0) {                                       // In this case: read all data from
          swSerial.read();                                                      // serial buffer and drop it.
        }
      }
    }
    return !serialRxbuffer.isEmpty();                                           // Return the status of the RX data queue.
  }

  RFDriver(const RFDriver&) = delete;                       // Define copy constructor.
  RFDriver& operator=(const RFDriver&) = delete;            // Define copy assignment operator.
  RFDriver(RFDriver&&) = delete;                            // Define move constructor.
  RFDriver& operator=(RFDriver&&) = delete;                 // Define move assignment operator.

private:

  /// @brief Calculates the 16bit CRC (XModem) of the given data.
  /// @param data Data whose CRC value should be calcilated.
  /// @param size Given data size in bytes.
  /// @return Returns with the calculated CRC value.
  uint16_t calCrc(const uint8_t* data, uint16_t size) {
    uint16_t crc = 0;                                         // Store CRC16 value.

    while(--size > 0) {                                       // Calculating CRC value.
      crc = crc ^ (uint16_t) *data++ << 8;
      uint8_t i = 8;                                          // Cycle variable.
      do {
        if(crc & 0x8000) {
          crc = crc << 1 ^ 0x1021;
        }
        else {
          crc = crc << 1;
        }
      }
      while(--i);
    }
    return (crc);                                             // Return with the calculated value.
  }

  SoftwareSerial swSerial;                    // Software serial object.
  CircularBuffer<RFData, 5> serialTxbuffer;   // Transmittable RF data queue.
  CircularBuffer<RFData, 5> serialRxbuffer;   // Received RF data queue.

};


#endif // RFDRIVER_HPP