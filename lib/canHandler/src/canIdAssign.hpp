#pragma once

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Wire format and admission rules for the SET_CAN_ID frame.
/// @details The gateway packs it, the node unpacks it and decides whether to obey. Both sides
/// call these helpers, so the layout and the rules live in one place rather than once per end -
/// and because the node's handler is AVR-only, this is also the part the host tests can reach.
/// The encoding is little-endian, matching the other CAN frames.
///
/// The node is addressed by the id it holds *now*, not only by the frame's receiver field. A
/// SET_CAN_ID that is repeated after the node already changed would otherwise be obeyed by
/// whichever node has since taken that id.
namespace CanIdAssign {
  static constexpr uint16_t idMask = 0x3FFU;  // The 10 bits the extended id reserves for an address.

  /// @brief Fields of a SET_CAN_ID frame.
  struct Request {
    uint16_t expectedLocal = 0U;              // The id the addressed node must currently hold.
    uint16_t newLocal = 0U;                   // The id it should take instead.
    bool reservedClear = false;               // Whether the unused bytes arrived zero, as a sender must leave them.
  };

  /// @brief Serializes a request into the 8 CAN data bytes.
  inline void pack(const Request& fields, uint8_t (&canData)[8]) {
    canData[0] = static_cast<uint8_t>(fields.expectedLocal & 0xFFU);
    canData[1] = static_cast<uint8_t>((fields.expectedLocal >> 8U) & 0xFFU);
    canData[2] = static_cast<uint8_t>(fields.newLocal & 0xFFU);
    canData[3] = static_cast<uint8_t>((fields.newLocal >> 8U) & 0xFFU);
    canData[4] = 0U;
    canData[5] = 0U;
    canData[6] = 0U;
    canData[7] = 0U;
  }

  /// @brief Parses the 8 CAN data bytes of a request back into its fields.
  [[nodiscard]] inline Request unpack(const uint8_t (&canData)[8]) {
    Request fields;
    fields.expectedLocal = static_cast<uint16_t>(
        static_cast<uint16_t>(canData[0]) |
        (static_cast<uint16_t>(canData[1]) << 8U));
    fields.newLocal = static_cast<uint16_t>(
        static_cast<uint16_t>(canData[2]) |
        (static_cast<uint16_t>(canData[3]) << 8U));
    fields.reservedClear = (canData[4] == 0U) && (canData[5] == 0U) && (canData[6] == 0U) && (canData[7] == 0U);
    return fields;
  }

  /// @brief Whether an id may be given to a node.
  /// @details Zero is what an unset address reads as - the ids default to it and a failed EEPROM
  /// load leaves them there - so it can never be handed out deliberately.
  /// @param id The candidate address.
  /// @return `true` when the id is usable as a node address.
  [[nodiscard]] inline bool isAssignableId(uint16_t id) {
    return (id != 0U) && (id <= idMask);
  }

  /// @brief Whether a node should obey the request it just received.
  /// @param request The unpacked frame.
  /// @param currentLocal The addressee's own id right now.
  /// @param currentMaster The master id the addressee holds.
  /// @param senderId The `from` field of the frame that carried the request.
  /// @return `true` when the change should be made.
  [[nodiscard]] inline bool isAcceptable(const Request& request, uint16_t currentLocal,
                                         uint16_t currentMaster, uint16_t senderId) {
    // Only the master may renumber a node. Nothing else in the protocol looks at who sent a
    // frame, and the receive filter matches on the addressee alone, so without this any node on
    // the bus could renumber any other.
    if(senderId != currentMaster) { return false; }
    if(!request.reservedClear) { return false; }
    if(request.expectedLocal != currentLocal) { return false; }
    if(!isAssignableId(request.newLocal)) { return false; }
    // Taking the master's id would leave the node filtering on the address it answers to.
    if(request.newLocal == currentMaster) { return false; }
    return true;
  }
} // namespace CanIdAssign
