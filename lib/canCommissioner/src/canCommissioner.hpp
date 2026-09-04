#pragma once
#if defined(ESP32) || defined(NATIVE_TEST)
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "canHandler.hpp"                                           /// CAN handler library.
#include "canIdAssign.hpp"                                          /// Provisional address and SET_CAN_ID rules.

/// @brief Gives an address to a CAN node that has none.
/// @details A node with nothing stored answers on an address it derived from its unique id and
/// announces itself there. No driver is built for that address, so the handler would drop the
/// frame; this picks it up instead, keeps what it heard, and hands the address on when told to.
///
/// What it keeps is a view of the bus, not a record of it: a node re-announces every few seconds,
/// so an entry dropped for want of room comes back on its own.
class CanCommissioner final : public MqttBase {
public:
  /// @brief Constructs the commissioner.
  /// @param canHandler CAN handler whose unclaimed frames this listens to.
  /// @param connectivity MQTT connection this reports through.
  /// @param subtopic Subtopic this answers on.
  CanCommissioner(CanHandler& canHandler, Connectivity& connectivity, const char* subtopic);

  /// @brief Starts listening for the announcements no driver claims.
  /// @return `true` always; nothing here can fail to start.
  [[nodiscard]] bool init() override;

  /// @brief Drops what has not been heard from for a while.
  /// @return `true` always.
  [[nodiscard]] bool run() override;

  /// @brief Handles `{"list":true}` and `{"assign":{"uid":"<16 hex>","id":<n>}}`.
  /// @param payloadJson The parsed message.
  void messageArrivedCallback(JsonVariant payloadJson) override;

  CanCommissioner(const CanCommissioner&) = delete;                 // Define copy constructor.
  CanCommissioner& operator=(const CanCommissioner&) = delete;      // Define copy assignment operator.
  CanCommissioner(CanCommissioner&&) = delete;                      // Define move constructor.
  CanCommissioner& operator=(CanCommissioner&&) = delete;           // Define move assignment operator.
  ~CanCommissioner() override = default;

private:
  static constexpr uint8_t maxWaiting = 4U;                         // Waiting nodes held at once; the rest re-announce.
  static constexpr uint32_t forgetTime = Time::secToMs(30U);        // Silence after which a node is taken off the list.
  static constexpr uint8_t uidHexLength = (CanIdAssign::uidLength * 2U) + 1U;  // Unique id as hex, plus the null.
  static constexpr uint16_t listBufSize = 224U;                     // {"waiting":[{"uid":"..","at":1023},...]} for maxWaiting.

  /// @brief A node heard announcing itself.
  struct Waiting {
    uint8_t uid[CanIdAssign::uidLength] = { 0U };                   // What the node calls itself.
    uint16_t provisionalId = 0U;                                    // Where it is answering meanwhile.
    uint32_t lastHeard = 0U;                                        // millis() of its last announcement.
    bool inUse = false;                                             // Whether this slot holds a node.
  };

  /// @brief Takes in one announcement.
  void noteAnnouncement(const CanHandler::CanFrame& frameIn);

  /// @brief Writes the waiting nodes as JSON.
  /// @param buffer Destination.
  /// @return `true` when the whole list fit.
  [[nodiscard]] bool formatWaiting(char (&buffer)[listBufSize]) const;

  /// @brief Sends SET_CAN_ID to the node with this unique id.
  /// @param uidHex The node's unique id as 16 hex characters.
  /// @param newLocalCanId The address it should take.
  /// @return `true` when a waiting node matched and the request reached the transmit queue.
  [[nodiscard]] bool assign(const char* uidHex, uint16_t newLocalCanId);

  CanHandler& canHandler;                                           // Where the announcements come from and the answer goes.
  Waiting waiting[maxWaiting];                                      // Nodes heard from, oldest overwritten when full.
};
#endif // ESP32 || NATIVE_TEST
