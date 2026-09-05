#include "canCommissioner.hpp"
#if defined(ESP32) || defined(NATIVE_TEST)
#include <stdio.h>                                                  /// snprintf for the JSON list.
#include <string.h>                                                 /// Byte and string comparison.

namespace {
  /// @brief Reads 16 hex characters into the 8 bytes they spell.
  /// @param hex Source string; anything shorter or not hex is refused.
  /// @param uid Destination.
  /// @return `true` when the whole id was read.
  [[nodiscard]] bool parseUid(const char* hex, uint8_t (&uid)[CanIdAssign::uidLength]) {
    if(hex == nullptr) { return false; }
    const char* cursor = hex;
    for(uint8_t& byte : uid) {
      uint8_t value = 0U;
      for(uint8_t nibble = 0U; nibble < 2U; nibble++) {
        const char c = *cursor;
        cursor++;
        uint8_t digit = 0U;
        if((c >= '0') && (c <= '9')) {
          digit = static_cast<uint8_t>(c - '0');
        } else if((c >= 'a') && (c <= 'f')) {
          digit = static_cast<uint8_t>((c - 'a') + 10);
        } else if((c >= 'A') && (c <= 'F')) {
          digit = static_cast<uint8_t>((c - 'A') + 10);
        } else {
          return false;
        }
        value = static_cast<uint8_t>((value << 4U) | digit);
      }
      byte = value;
    }
    // Anything past the last pair means the caller named something longer than a unique id.
    return *cursor == '\0';
  }

  /// @brief Writes a unique id as the hex characters that spell it.
  /// @param uid The id to write.
  /// @param hex Destination, one character per nibble plus the null.
  void formatUid(const uint8_t (&uid)[CanIdAssign::uidLength], char (&hex)[(CanIdAssign::uidLength * 2U) + 1U]) {
    char* cursor = hex;
    for(const uint8_t byte : uid) {
      (void)snprintf(cursor, 3U, "%02x", byte);
      cursor += 2;
    }
  }

} // namespace

CanCommissioner::CanCommissioner(CanHandler& canHandler, Connectivity& connectivity, const char* subtopic) :
  MqttBase(connectivity, subtopic),
  canHandler(canHandler) {}

bool CanCommissioner::init() {
  canHandler.setUnclaimedFrameCallback([](void* ctx, const CanHandler::CanFrame& frameIn) -> void {
    static_cast<CanCommissioner*>(ctx)->noteAnnouncement(frameIn);
  },
                                       this);
  return true;
}

bool CanCommissioner::run() {
  const uint32_t actualTime = millis();
  for(Waiting& entry : waiting) {
    if(entry.inUse && Time::hasElapsed(actualTime, entry.lastHeard, forgetTime)) {
      entry.inUse = false;
      Logger::get()->printf_P(PSTR("[CAN] Node on %hu stopped announcing\r\n"), entry.provisionalId);
    }
  }
  return true;
}

void CanCommissioner::noteAnnouncement(const CanHandler::CanFrame& frameIn) {
  if(static_cast<uint16_t>(frameIn.cmd) != static_cast<uint16_t>(CanCmd::ANNOUNCE)) { return; }
  const uint16_t from = static_cast<uint16_t>(frameIn.from);
  if(!CanIdAssign::isProvisionalId(from)) { return; }

  Waiting* slot = nullptr;
  Waiting* oldest = &waiting[0];
  for(Waiting& entry : waiting) {
    if(entry.inUse && (memcmp(entry.uid, frameIn.data, sizeof(entry.uid)) == 0)) {
      slot = &entry;
      break;
    }
    if(!entry.inUse) { slot = (slot == nullptr) ? &entry : slot; }
    if(entry.lastHeard < oldest->lastHeard) { oldest = &entry; }
  }
  // Full, and none of them is this node: the one heard from longest ago makes way. It announces
  // again in a few seconds, so nothing is lost for good.
  if(slot == nullptr) { slot = oldest; }

  const bool isNew = !slot->inUse || (memcmp(slot->uid, frameIn.data, sizeof(slot->uid)) != 0);
  memcpy(slot->uid, frameIn.data, sizeof(slot->uid));
  slot->provisionalId = from;
  slot->lastHeard = millis();
  slot->inUse = true;
  if(isNew) { Logger::get()->printf_P(PSTR("[CAN] Node waiting for an address on %hu\r\n"), from); }
}

bool CanCommissioner::formatWaiting(char (&buffer)[listBufSize]) const {
  int32_t written = snprintf_P(buffer, listBufSize, PSTR(R"({"waiting":[)"));
  if(written < 0) { return false; }
  bool first = true;
  for(const Waiting& entry : waiting) {
    if(!entry.inUse) { continue; }
    char uidHex[uidHexLength] = { '\0' };
    formatUid(entry.uid, uidHex);
    const int32_t added = snprintf_P(&buffer[written], static_cast<size_t>(listBufSize - written),
                                     PSTR(R"(%s{"uid":"%s","at":%hu})"), first ? "" : ",", uidHex, entry.provisionalId);
    if((added < 0) || (added >= static_cast<int32_t>(listBufSize - written))) { return false; }
    written += added;
    first = false;
  }
  const int32_t tail = snprintf_P(&buffer[written], static_cast<size_t>(listBufSize - written), PSTR("]}"));
  return (tail >= 0) && (tail < static_cast<int32_t>(listBufSize - written));
}

bool CanCommissioner::assign(const char* uidHex, uint16_t newLocalCanId) {
  uint8_t uid[CanIdAssign::uidLength] = { 0U };
  if(!parseUid(uidHex, uid)) { return false; }
  // Naming an address is the caller's word that it is free: nothing here can see whether
  // something already answers on it.
  if(!CanIdAssign::isAssignableId(newLocalCanId)) { return false; }

  for(const Waiting& entry : waiting) {
    if(!entry.inUse || (memcmp(entry.uid, uid, sizeof(uid)) != 0)) { continue; }
    CanIdAssign::Request request;
    request.expectedLocal = entry.provisionalId;
    request.newLocal = newLocalCanId;
    uint8_t canData[8] = { 0U };
    CanIdAssign::pack(request, canData);
    Logger::get()->printf_P(PSTR("[CAN] Address %hu -> %hu\r\n"), entry.provisionalId, newLocalCanId);
    return canHandler.send(CanHandler::CanFrame{ entry.provisionalId, static_cast<uint16_t>(CanCmd::SET_CAN_ID),
                                                 canHandler.getLocalCanId(), canData });
  }
  return false;
}

void CanCommissioner::messageArrivedCallback(JsonVariant payloadJson) {
  if(payloadJson[F("list")].is<bool>()) {
    char listBuffer[listBufSize] = { '\0' };
    // Answered rather than published: whoever asked may only be able to read the subtopic the
    // question came in on.
    (void)sendReply(formatWaiting(listBuffer) ? listBuffer : PSTR(R"({"waiting":[]})"));
    return;
  }

  JsonVariant assignJsonVar = payloadJson[F("assign")];
  if(assignJsonVar.is<JsonObject>()) {
    const bool sent = assign(assignJsonVar[F("uid")].as<const char*>(), assignJsonVar[F("id")].as<uint16_t>());
    (void)sendResponse(sent ? Response::ACK : Response::NACK, static_cast<uint16_t>(CanCmd::SET_CAN_ID));
  }
}
#endif // ESP32 || NATIVE_TEST
