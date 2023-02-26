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
      swSerial.write(reinterpret_cast<uint8_t*>(&rfData), sizeof(RFData));      // Send data.
    }

    if(swSerial.available() >= (int16_t)sizeof(RFData)) {                       // Check if serial data arrived.
      RFData rfData;
      swSerial.readBytes(reinterpret_cast<uint8_t*>(&rfData), sizeof(RFData));  // Read arrived bytes to struct.
      if((rfData.beginFrame == '*') && (rfData.endFrame == '#')) {              // Check if frame begin and end are ok.
        serialRxbuffer.put(rfData);                                             // If yes, put the RF data struct to queue.
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

  SoftwareSerial swSerial;                    // Software serial object.
  CircularBuffer<RFData, 5> serialTxbuffer;   // Transmittable RF data queue.
  CircularBuffer<RFData, 5> serialRxbuffer;   // Received RF data queue.

};


#endif // RFDRIVER_HPP