#pragma once
#include "RF433Sender.hpp"
#include "crc8.hpp"

class RfDataTransmitter {
private:
  struct __attribute__((packed)) RfFrame {
    uint8_t sensorType;
    uint8_t sensorId;
    uint8_t data[4];
    uint8_t crc;

    RfFrame() : sensorType(0), sensorId(0), data{0}, crc(0) {}

    RfFrame(uint8_t sensorType, uint8_t sensorId, const uint8_t (&extData)[sizeof(data)]) :
      sensorType(sensorType),
      sensorId(sensorId),
      crc(0)
    {
      memcpy(data, extData, sizeof(data));
    }
  };

public:
  // Constructor takes reference to pre-created RF433Sender and encryption hash array
  RfDataTransmitter(RF433Sender& rfSender, const uint8_t (&hash)[sizeof(RfFrame)]) :
    rfSender(rfSender), encryptionHash(hash) {
  }

  // Default destructor
  ~RfDataTransmitter() = default;

  // Main function to transmit data
  void transmit(uint8_t sensorType, uint8_t sensorId, const uint8_t (&data)[sizeof(RfFrame::data)]) {
    // Create the frame
    RfFrame frame(sensorType, sensorId, data);

    // Calculate CRC8 of the frame (excluding the CRC field itself)
    frame.crc = Crc8::calculate(reinterpret_cast<uint8_t*>(&frame), sizeof(RfFrame) - 1);

    // Apply XOR encryption
    applyEncryption(frame);

    // Send the encrypted frame via RF
    rfSender.send(reinterpret_cast<uint8_t*>(&frame), sizeof(RfFrame) * 8); // Convert bytes to bits
  }

private:
  // XOR the entire struct with the hash
  void applyEncryption(RfFrame& frame) {
    uint8_t* frameBytes = reinterpret_cast<uint8_t*>(&frame);

    // XOR each byte of the frame with corresponding hash byte
    for (uint8_t i = 0; i < sizeof(RfFrame); i++) {
      frameBytes[i] ^= encryptionHash[i];
    }
  }

  RF433Sender& rfSender;
  const uint8_t (&encryptionHash)[sizeof(RfFrame)];
};