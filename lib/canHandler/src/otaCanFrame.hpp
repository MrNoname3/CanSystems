#ifndef OTA_CAN_FRAME_HPP
#define OTA_CAN_FRAME_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Wire format for the OTA frames exchanged over CAN between the gateway (the sender,
/// canMqttGateway) and the CAN device (the receiver, canHandlerAtmega328P).
/// @details The 8 data bytes of an OTA_START / OTA_SEND frame are laid out here in exactly one
/// place. Both the packer (gateway) and the unpacker (device) call these helpers, so the byte
/// order can no longer silently diverge between the two sides: a change to the layout is a change
/// to a single function, and the round-trip / contract tests guard it. The encoding is
/// little-endian, matching the original hand-written shifts on both ends.
namespace OtaCanFrame {
  // Number of firmware data bytes carried by a single OTA_SEND frame (must equal OTA::fwPieceSize;
  // the device side static-asserts that). The remaining 4 bytes hold the byte offset.
  static constexpr uint8_t dataPieceSize = 4U;

  /// @brief Fields of an OTA_START frame: where to store, how much, and the expected checksum.
  struct StartFrame {
    uint16_t storageNumber = 0U;              // Flash block number on the device (a.k.a. otaFlashBegin).
    uint32_t fwSize = 0U;                     // Total firmware size in bytes.
    uint16_t fwCrc = 0U;                      // CRC16 of the whole firmware.
  };

  /// @brief Fields of an OTA_SEND frame: one firmware piece and the byte offset it belongs to.
  struct SendFrame {
    uint8_t data[dataPieceSize] = { 0U };       // Firmware bytes for this piece (trailing bytes 0 on the last, short piece).
    uint32_t dataAddress = 0U;                // Byte offset of this piece within the firmware.
  };

  /// @brief Serializes an OTA_START frame into the 8 CAN data bytes.
  inline void packStart(const StartFrame& fields, uint8_t (&canData)[8]) {
    canData[0] = static_cast<uint8_t>(fields.storageNumber & 0xFFU);
    canData[1] = static_cast<uint8_t>((fields.storageNumber >> 8U) & 0xFFU);
    canData[2] = static_cast<uint8_t>(fields.fwSize & 0xFFU);
    canData[3] = static_cast<uint8_t>((fields.fwSize >> 8U) & 0xFFU);
    canData[4] = static_cast<uint8_t>((fields.fwSize >> 16U) & 0xFFU);
    canData[5] = static_cast<uint8_t>((fields.fwSize >> 24U) & 0xFFU);
    canData[6] = static_cast<uint8_t>(fields.fwCrc & 0xFFU);
    canData[7] = static_cast<uint8_t>((fields.fwCrc >> 8U) & 0xFFU);
  }

  /// @brief Parses the 8 CAN data bytes of an OTA_START frame back into its fields.
  [[nodiscard]] inline StartFrame unpackStart(const uint8_t (&canData)[8]) {
    StartFrame fields;
    fields.storageNumber = static_cast<uint16_t>(
        static_cast<uint16_t>(canData[0]) |
        (static_cast<uint16_t>(canData[1]) << 8U));
    fields.fwSize =
        static_cast<uint32_t>(canData[2]) |
        (static_cast<uint32_t>(canData[3]) << 8U) |
        (static_cast<uint32_t>(canData[4]) << 16U) |
        (static_cast<uint32_t>(canData[5]) << 24U);
    fields.fwCrc = static_cast<uint16_t>(
        static_cast<uint16_t>(canData[6]) |
        (static_cast<uint16_t>(canData[7]) << 8U));
    return fields;
  }

  /// @brief Serializes an OTA_SEND frame into the 8 CAN data bytes.
  inline void packSend(const SendFrame& fields, uint8_t (&canData)[8]) {
    canData[0] = fields.data[0];
    canData[1] = fields.data[1];
    canData[2] = fields.data[2];
    canData[3] = fields.data[3];
    canData[4] = static_cast<uint8_t>(fields.dataAddress & 0xFFU);
    canData[5] = static_cast<uint8_t>((fields.dataAddress >> 8U) & 0xFFU);
    canData[6] = static_cast<uint8_t>((fields.dataAddress >> 16U) & 0xFFU);
    canData[7] = static_cast<uint8_t>((fields.dataAddress >> 24U) & 0xFFU);
  }

  /// @brief Parses the 8 CAN data bytes of an OTA_SEND frame back into its fields.
  [[nodiscard]] inline SendFrame unpackSend(const uint8_t (&canData)[8]) {
    SendFrame fields;
    fields.data[0] = canData[0];
    fields.data[1] = canData[1];
    fields.data[2] = canData[2];
    fields.data[3] = canData[3];
    fields.dataAddress =
        static_cast<uint32_t>(canData[4]) |
        (static_cast<uint32_t>(canData[5]) << 8U) |
        (static_cast<uint32_t>(canData[6]) << 16U) |
        (static_cast<uint32_t>(canData[7]) << 24U);
    return fields;
  }
} // namespace OtaCanFrame
#endif // OTA_CAN_FRAME_HPP
