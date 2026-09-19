#include "PubSubClient.h"
#include "Arduino.h"
#include <utility>   // std::move for the callback the setters take by value

PubSubClient::PubSubClient(Client& client) :
  tcpClient(client) {
}
PubSubClient::PubSubClient(const IPAddress& addr, uint16_t port, Client& client) :
  tcpClient(client) {
  setServer(addr, port);
}

PubSubClient::PubSubClient(const uint8_t* ip, uint16_t port, MqttCallback callback, Client& client) :
  tcpClient(client) {
  setServer(ip, port);
  setCallback(std::move(callback));
}
PubSubClient::PubSubClient(const char* domain, uint16_t port, MqttCallback callback, Client& client) :
  tcpClient(client) {
  setServer(domain, port);
  setCallback(std::move(callback));
}
bool PubSubClient::connect(const char* id) {
  return connect(id, nullptr, nullptr, nullptr, 0U, false, nullptr, true);
}

bool PubSubClient::connect(const char* id, const char* user, const char* pass) {
  return connect(id, user, pass, nullptr, 0U, false, nullptr, true);
}

bool PubSubClient::connect(const char* id, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage) {
  return connect(id, nullptr, nullptr, willTopic, willQos, willRetain, willMessage, true);
}

bool PubSubClient::connect(const char* id, const char* user, const char* pass, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage) {
  return connect(id, user, pass, willTopic, willQos, willRetain, willMessage, true);
}

bool PubSubClient::connect(const char* id, const char* user, const char* pass, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage, bool cleanSession) {
  // Bits 3 and 4 of the flags byte hold the will qos, and the will-retain and clean-session flags
  // sit beside them: a level too wide for those two bits is shifted straight onto them.
  if((willTopic != nullptr) && ((willQos > 2U) || !topicNameValid(willTopic))) { return false; }
  // The client id is the one field every CONNECT carries [MQTT-3.1.3-3], and an empty one asks the
  // broker to name this client, which it only does for a clean session [MQTT-3.1.3-7].
  if((id == nullptr) || ((id[0] == '\0') && !cleanSession)) { return false; }
  if(connected()) { return true; }
  // A CONNECT is the first packet of a network connection, and no connection carries a second one
  // [MQTT-3.1.0-2]. A socket still open here belongs to a session that ended without it - one the
  // broker may well still be holding - so it is dropped rather than written down.
  if(tcpClient.connected() != 0) { tcpClient.stop(); }
  const bool result = static_cast<bool>(domain != nullptr ? tcpClient.connect(this->domain, this->port)
                                                          : tcpClient.connect(this->ip, this->port));
  if(!result) {
    connectionState = State::CONNECT_FAILED;
    tcpClient.stop();
    return false;
  }
  nextMsgId = 0U;   // Stepped before use, so the first id of the session is 1.
  // Half a packet belongs to the session it was arriving on; this one starts the stream over.
  resetReader();
  const uint16_t length = buildConnectPacket(id, user, pass, willTopic, willQos, willRetain, willMessage, cleanSession);
  // Zero means a string did not fit; checkStringLength() has already stopped the client.
  if(length == 0U) { return false; }
  if(!write(MQTTCONNECT, this->buffer, length - MQTT_MAX_HEADER_SIZE)) {
    // The link took less than the whole packet, so no CONNACK is coming: waiting for one anyway
    // would hold the caller for the socket timeout and then name it as the reason.
    connectionState = State::CONNECTION_LOST;
    tcpClient.stop();
    return false;
  }
  lastInActivity = lastOutActivity = millis();
  return awaitConnAck();
}

uint16_t PubSubClient::buildConnectPacket(const char* id, const char* user, const char* pass, const char* willTopic,
                                          uint8_t willQos, bool willRetain, const char* willMessage, bool cleanSession) {
  // Leave room in the buffer for header and variable length field
  uint16_t length = MQTT_MAX_HEADER_SIZE;

#if MQTT_VERSION == MQTT_VERSION_3_1
  const uint8_t d[9] = { 0x00U, 0x06U, 'M', 'Q', 'I', 's', 'd', 'p', MQTT_VERSION };
#elif MQTT_VERSION == MQTT_VERSION_3_1_1
  const uint8_t d[7] = { 0x00U, 0x04U, 'M', 'Q', 'T', 'T', MQTT_VERSION };
#endif
  memcpy(this->buffer + length, d, sizeof(d));
  length = static_cast<uint16_t>(length + sizeof(d));

  uint8_t v = (willTopic != nullptr)
                  ? static_cast<uint8_t>(0x04U | (willQos << 3U) | (willRetain ? 0x20U : 0x00U))
                  : 0x00U;
  v |= cleanSession ? 0x02U : 0x00U;
  v |= (user != nullptr) ? 0x80U : 0x00U;
  v |= (user != nullptr && pass != nullptr) ? 0x40U : 0x00U;
  this->buffer[length++] = v;

  this->buffer[length++] = static_cast<uint8_t>(this->keepAlive >> 8U);
  this->buffer[length++] = static_cast<uint8_t>(this->keepAlive & 0xFFU);

  if(!checkStringLength(length, id)) {
    return 0U;
  }
  length = writeString(id, this->buffer, length);
  if(willTopic != nullptr) {
    const char* const willMsg = (willMessage != nullptr) ? willMessage : "";
    if(!checkStringLength(length, willTopic)) {
      return 0U;
    }
    length = writeString(willTopic, this->buffer, length);
    if(!checkStringLength(length, willMsg)) {
      return 0U;
    }
    length = writeString(willMsg, this->buffer, length);
  }

  if(user != nullptr) {
    if(!checkStringLength(length, user)) {
      return 0U;
    }
    length = writeString(user, this->buffer, length);
    if(pass != nullptr) {
      if(!checkStringLength(length, pass)) {
        return 0U;
      }
      length = writeString(pass, this->buffer, length);
    }
  }

  return length;
}

bool PubSubClient::awaitConnAck() {
  const uint32_t socketTimeoutMs = static_cast<uint32_t>(this->socketTimeout) * 1000U;
  while(tcpClient.available() == 0) {
    yield();
    const uint32_t t = millis();
    if(t - lastInActivity >= socketTimeoutMs) {
      connectionState = State::CONNECTION_TIMEOUT;
      tcpClient.stop();
      return false;
    }
  }
  const RxResult connAck = readPacketBlocking();
  // The first packet from the server is a CONNACK [MQTT-3.2.0-1], four bytes long. Read anywhere
  // else, the return code is whatever sits at that offset: a PUBACK for message 0x1200 accepts.
  // Its second byte carries the acknowledge flags: bits 7-1 are reserved and come as zero, and
  // bit 0 offers a session the broker kept for this client id. This client keeps none of its own
  // - no subscription and no unfinished delivery outlive a connection here - so a session to pick
  // up is one it cannot hold up its end of, and [MQTT-3.2.2-2] closes on it.
  const bool connAckValid = (connAck == RxResult::Complete) && (rxLen == 4U) &&
                            ((this->buffer[0] & 0xF0U) == MQTTCONNACK) && (this->buffer[2] == 0x00U);
  // A packet that did arrive whole and is not the CONNACK is a protocol violation, not a link that
  // went quiet; [MQTT-4.8.0-1] closes on those, and the state says which of the two it was.
  const State connAckFailure = (connAck == RxResult::Complete) ? State::PROTOCOL_ERROR : readFailureState(connAck);
  const uint8_t connAckCode = connAckValid ? this->buffer[3] : 0xFFU;
  // The reader has to start clean for the session: whatever it kept about the CONNACK would
  // otherwise be finished a second time on the first loop(), before any real packet is read.
  resetReader();

  if(connAckValid) {
    if(connAckCode == 0U) {
      lastInActivity = millis();
      pingOutstanding = false;
      // A ping the last session's client would not take belongs to that session. Left standing,
      // its deadline is already past, so the first ping this one cannot hand over ends the
      // connection on the spot instead of being retried.
      pingUnsent = false;
      pingReasked = false;
      connectionState = State::CONNECTED;
      return true;
    }
    // The standard names return codes 1 to 5; a broker answering anything else has refused all
    // the same, and the answer is reported as the refusal it is rather than as a state code.
    connectionState = (connAckCode <= highestNamedConnAckCode) ? static_cast<State>(connAckCode) : State::CONNECT_REFUSED;
  } else {
    // Nothing came, or what came was not a CONNACK. Leaving the state alone would report whatever
    // ended the last session as the reason this one never started.
    connectionState = connAckFailure;
  }
  tcpClient.stop();
  return false;
}

bool PubSubClient::awaitSubAck(uint16_t packetId) {
  const uint32_t startMs = millis();
  const uint32_t timeoutMs = static_cast<uint32_t>(this->socketTimeout) * 1000U;
  bool granted = false;
  bool waiting = true;
  while(waiting) {
    const RxResult result = readPacketBlocking();
    if(result != RxResult::Complete) {
      connectionState = readFailureState(result);
      tcpClient.stop();
      waiting = false;
    } else if(isSubAckFor(packetId)) {
      // One filter goes out per SUBSCRIBE, so the first return code is the one that answers it.
      const uint8_t returnCode = this->buffer[rxLengthLength + 3U];
      if((returnCode > subscribeMaxGrantedQos) && (returnCode != subscribeFailureCode)) {
        // "SUBACK return codes other than 0x00, 0x01, 0x02 and 0x80 are reserved and MUST NOT be
        // used" [MQTT-3.9.3-2]. Read as a grant, one of those would leave the client listening at
        // a level the broker never named.
        connectionState = State::PROTOCOL_ERROR;
        tcpClient.stop();
      } else {
        granted = (returnCode != subscribeFailureCode);
      }
      waiting = false;
    } else {
      // The broker got a word in first; it is this session's traffic and is answered as such.
      const uint16_t len = rxLen;
      const uint8_t llen = rxLengthLength;
      resetReader();
      if(!dispatchPacket(len, llen)) {
        connectionState = State::PROTOCOL_ERROR;
        tcpClient.stop();
        waiting = false;
      } else {
        waiting = ((millis() - startMs) < timeoutMs);
      }
    }
  }
  resetReader();
  return granted;
}

bool PubSubClient::isSubAckFor(uint16_t packetId) const {
  if(((this->buffer[0] & 0xF0U) != MQTTSUBACK) || (rxLen < (rxLengthLength + 4U))) {
    return false;
  }
  const uint16_t acked = static_cast<uint16_t>((this->buffer[rxLengthLength + 1U] << 8U) + this->buffer[rxLengthLength + 2U]);
  return acked == packetId;
}

bool PubSubClient::checkStringLength(uint16_t length, const char* str) const {
  const bool fits = (length + 2U + strnlen(str, this->bufferSize) <= this->bufferSize);
  if(!fits) {
    tcpClient.stop();
  }
  return fits;
}

void PubSubClient::resetReader() {
  rxPhase = RxPhase::Idle;
  rxLen = 0U;
  rxLengthLength = 0U;
  rxMultiplier = 1U;
  rxRemaining = 0U;
  rxPayloadDone = 0U;
}

bool PubSubClient::topicNameValid(const char* topic) const {
  const size_t len = strnlen(topic, this->bufferSize);
  if(len == 0U) { return false; }
  return (memchr(topic, '+', len) == nullptr) && (memchr(topic, '#', len) == nullptr);
}

bool PubSubClient::topicFilterValid(const char* filter) const {
  const size_t len = strnlen(filter, this->bufferSize);
  if(len == 0U) { return false; }
  for(size_t i = 0U; i < len; i++) {
    // Both wildcards stand for a whole level, so each has to be bounded by separators or by the
    // ends of the filter; the multi-level one has nothing after it at all.
    const bool levelStarts = (i == 0U) || (filter[i - 1U] == '/');
    const bool levelEnds = (i == (len - 1U)) || (filter[i + 1U] == '/');
    if(filter[i] == '#') {
      if(!levelStarts || (i != (len - 1U))) { return false; }
    } else if(filter[i] == '+') {
      if(!levelStarts || !levelEnds) { return false; }
    } else {
      // An ordinary character, which any level may hold.
    }
  }
  return true;
}

bool PubSubClient::fixedHeaderValid(uint8_t header) {
  const uint8_t type = header & 0xF0U;
  const uint8_t flags = header & 0x0FU;
  if((type == 0U) || (type == MQTTReserved)) { return false; }   // neither number names a packet
  // Table 2.1 gives each type its direction, and these five travel to the broker alone. One coming
  // the other way is a packet no server sends, which puts the stream out of step with what it is
  // read as: a SUBSCRIBE and a SUBACK differ by a nibble, and the session ends here either way.
  if((type == MQTTCONNECT) || (type == MQTTSUBSCRIBE) || (type == MQTTUNSUBSCRIBE) ||
     (type == MQTTPINGREQ) || (type == MQTTDISCONNECT)) { return false; }
  // A PUBLISH spends its low nibble on dup, qos and retain. Two of the three are its own to set:
  // the standard reserves one qos level and gives no delivery protocol for it, and leaves the dup
  // flag clear at qos 0 [MQTT-3.3.1-2], where nothing is acknowledged and so nothing is sent again.
  if(type == MQTTPUBLISH) {
    const uint8_t publishQos = flags & 0x06U;
    if(publishQos == 0x06U) { return false; }
    return (publishQos != MQTTQOS0) || ((flags & 0x08U) == 0U);
  }
  // Of the three types carrying 0b0010 only PUBREL reaches a client; the other two were turned
  // back above.
  if(type == MQTTPUBREL) { return flags == 0x02U; }
  return flags == 0U;
}

bool PubSubClient::remainingLengthValid(uint8_t header, uint32_t remaining) {
  const uint8_t type = header & 0xF0U;
  // A PINGRESP is its fixed header and nothing else.
  if(type == MQTTPINGRESP) { return remaining == 0U; }
  // A CONNACK is its flags byte and its return code; every acknowledgement below is the two bytes
  // of the packet identifier it answers.
  if((type == MQTTCONNACK) || (type == MQTTPUBACK) || (type == MQTTPUBREC) ||
     (type == MQTTPUBREL) || (type == MQTTPUBCOMP) || (type == MQTTUNSUBACK)) { return remaining == 2U; }
  // A SUBACK carries a return code for each filter of the SUBSCRIBE it answers, in the order they
  // were asked for [MQTT-3.9.3-1]. One filter goes out per SUBSCRIBE here, so one code comes back;
  // more than that answers a packet this client did not send.
  if(type == MQTTSUBACK) { return remaining == 3U; }
  // The topic-length field is two bytes, and the remaining length counts them. A PUBLISH that
  // announces fewer has none to give: the payload length derived from it would wrap to nearly 4 GB.
  if(type == MQTTPUBLISH) { return remaining >= 2U; }
  return true;
}

PubSubClient::State PubSubClient::readFailureState(RxResult result) {
  if(result == RxResult::TooLarge) { return State::PACKET_TOO_LARGE; }
  if(result == RxResult::Malformed) { return State::PROTOCOL_ERROR; }
  return State::CONNECTION_TIMEOUT;
}

PubSubClient::RxResult PubSubClient::readPacketBlocking() {
  const uint32_t timeoutMs = static_cast<uint32_t>(this->socketTimeout) * 1000U;
  const uint32_t startMs = millis();
  // What the reader already has is carried on rather than dropped: the bytes it has taken cannot
  // be put back, and the rest of that packet is in the stream ahead of whatever is waited for here.
  while(true) {
    if(rxPhase == RxPhase::Payload) {
      if(advancePayload() == RxResult::Complete) { return RxResult::Complete; }
    } else {
      const RxResult header = advanceHeader();
      if((header == RxResult::Malformed) || (header == RxResult::TooLarge)) { return header; }
      // Header still coming; the timeout below is what ends the wait.
    }
    if((millis() - startMs) >= timeoutMs) { return RxResult::Incomplete; }
    yield();
  }
}

PubSubClient::RxResult PubSubClient::advanceHeader() {
  while(tcpClient.available() != 0) {
    const uint8_t byteIn = static_cast<uint8_t>(tcpClient.read());
    if(rxLen == 0U) {
      // "If invalid flags are received, the receiver MUST close the Network Connection"
      // [MQTT-2.2.2-2], which covers the reserved QoS level [MQTT-3.3.1-4] as well: read as a QoS 0
      // message it would hand the callback the packet identifier as the first two payload bytes.
      if(!fixedHeaderValid(byteIn)) { return RxResult::Malformed; }
      this->buffer[0] = byteIn;
      rxLen = 1U;
      continue;
    }
    this->buffer[rxLen] = byteIn;
    rxLen++;
    rxRemaining += (byteIn & 127U) * rxMultiplier;
    rxMultiplier <<= 7U;  // multiplier *= 128
    if((byteIn & 128U) == 0U) {
      rxLengthLength = static_cast<uint8_t>(rxLen - 1U);
      // Malformed the same way an invalid remaining length is, and dropped the same way.
      if(!remainingLengthValid(this->buffer[0], rxRemaining)) { return RxResult::Malformed; }
      // A packet with nowhere to go is an internal buffer full condition, which [MQTT-4.8.0-2]
      // answers by ending the connection rather than by reading bytes that cannot be kept.
      if((rxLen + rxRemaining) > this->bufferSize) { return RxResult::TooLarge; }
      rxPhase = RxPhase::Payload;
      return RxResult::Complete;
    }
    if(rxLen == 5U) { return RxResult::Malformed; }  // Invalid remaining-length encoding.
  }
  return RxResult::Incomplete;
}

PubSubClient::RxResult PubSubClient::advancePayload() {
  while(rxPayloadDone < rxRemaining) {
    const int ready = tcpClient.available();
    if(ready <= 0) { return RxResult::Incomplete; }
    const uint32_t left = rxRemaining - rxPayloadDone;
    const uint32_t take = (static_cast<uint32_t>(ready) < left) ? static_cast<uint32_t>(ready) : left;
    // What the client hands over is what was taken: counting the request instead would walk the
    // parse position past bytes still on the socket, and every packet after it would be misread.
    const int got = tcpClient.read(&this->buffer[rxLen], take);
    if(got <= 0) { return RxResult::Incomplete; }
    rxLen = static_cast<uint16_t>(rxLen + static_cast<uint32_t>(got));
    rxPayloadDone += static_cast<uint32_t>(got);
  }
  return RxResult::Complete;
}

bool PubSubClient::dispatchPacket(uint16_t len, uint8_t llen) {
  const uint8_t type = this->buffer[0] & 0xF0U;
  if(type == MQTTPUBLISH) { return dispatchPublish(len, llen); }
  if(type == MQTTCONNACK) {
    // A session opens with one CONNACK and the handshake reads it [MQTT-3.2.0-1]; a second is
    // the broker answering a CONNECT this client never sent it.
    return false;
  }
  if(type == MQTTPINGRESP) {
    pingOutstanding = false;
    return true;
  }
  if((type == MQTTPUBACK) || (type == MQTTPUBREC) || (type == MQTTPUBREL) || (type == MQTTPUBCOMP)) {
    // Each answers a delivery the sender of the packet identifier it carries put in flight: a
    // PUBACK the QoS 1 PUBLISH this client sent, the other three a QoS 2 exchange. This client
    // never publishes above QoS 0 and never lets a QoS 2 delivery start [MQTT-3.8.4-6], so none
    // of the four is ever a delivery this side owns, whatever identifier it names.
    return false;
  }
  return true;
}

bool PubSubClient::dispatchPublish(uint16_t len, uint8_t llen) {
  // Nothing above QoS 1 is ever subscribed for, so a QoS 2 delivery is one no conforming broker
  // sends [MQTT-3.8.4-6]; read as less, its packet identifier lands on the front of the payload.
  if((this->buffer[0] & 0x06U) == MQTTQOS2) {
    return false;
  }
  const uint16_t tl = static_cast<uint16_t>((this->buffer[llen + 1U] << 8U) + this->buffer[llen + 2U]); /* topic length in bytes */
  // The topic length and the packet length are two independent numbers off the wire, and every
  // index below is built from the first one. A packet where they disagree is a protocol
  // violation, and [MQTT-4.8.0-1] answers those by closing the connection.
  const uint16_t msgIdLen = ((this->buffer[0] & 0x06U) == MQTTQOS1) ? 2U : 0U;   // msgId only present for QOS>0
  if(len < (static_cast<uint32_t>(llen) + 3U + tl + msgIdLen)) {
    return false;
  }
  // "All Topic Names and Topic Filters MUST be at least one character long" [MQTT-4.7.3-1];
  // an empty one names nothing the callback could tell this message apart by.
  if(tl == 0U) {
    return false;
  }
  // A string carrying U+0000 closes the connection [MQTT-1.5.3-2]: the topic reaches the
  // callback as a C string, which would end at that byte and hide what the message was about.
  if(memchr(this->buffer + llen + 3U, 0, tl) != nullptr) {
    return false;
  }
  // A topic name says where a message was published; the wildcards belong to the filters it is
  // matched against, and a PUBLISH must not carry one [MQTT-3.3.2-2]. The callback routes on
  // this string, and would be handed a pattern to route by.
  const uint8_t* const topicName = this->buffer + llen + 3U;
  if((memchr(topicName, '+', tl) != nullptr) || (memchr(topicName, '#', tl) != nullptr)) {
    return false;
  }
  // Taken before the callback runs, as the acknowledgement is built after it: a callback that
  // publishes writes its own packet over the one being read here.
  const uint16_t msgId = (msgIdLen != 0U)
                             ? static_cast<uint16_t>((this->buffer[llen + 3U + tl] << 8U) + this->buffer[llen + 3U + tl + 1U])
                             : 0U;
  // "Each time a Client sends a new packet of one of these types it MUST assign it a currently
  // unused Packet Identifier" [MQTT-2.3.1-1], and zero is never one of those: acknowledged
  // back, it names no delivery the broker can close off, and the message would stay in flight.
  if((msgIdLen != 0U) && (msgId == 0U)) {
    return false;
  }
  if(callback != nullptr) {
    memmove(this->buffer + llen + 2U, this->buffer + llen + 3U, tl);                                      /* move topic inside buffer 1 byte to front */
    this->buffer[llen + 2U + tl] = 0U;                                                                    /* end the topic as a 'C' string with \x00 */
    char* const topic = reinterpret_cast<char*>(this->buffer + llen + 2U);
    uint8_t* const payload = this->buffer + llen + 3U + tl + msgIdLen;
    callback(topic, payload, len - llen - 3U - tl - msgIdLen);
  }
  if(msgIdLen != 0U) {
    // Owed by the protocol rather than by the application, and sent the way every other packet
    // is: a link that took none of it has acknowledged nothing, and counting the attempt as
    // outgoing traffic would put the keep-alive ping off by an interval the broker does not wait.
    this->buffer[MQTT_MAX_HEADER_SIZE] = static_cast<uint8_t>(msgId >> 8U);
    this->buffer[MQTT_MAX_HEADER_SIZE + 1U] = static_cast<uint8_t>(msgId & 0xFFU);
    (void)write(MQTTPUBACK, this->buffer, 2U);
  }
  return true;
}

bool PubSubClient::settleReader() {
  if(rxPhase == RxPhase::Idle) { return true; }
  const RxResult result = readPacketBlocking();
  if(result != RxResult::Complete) {
    connectionState = readFailureState(result);
    tcpClient.stop();
    resetReader();
    return false;
  }
  const uint16_t len = rxLen;
  const uint8_t llen = rxLengthLength;
  resetReader();
  if(!dispatchPacket(len, llen)) {
    connectionState = State::PROTOCOL_ERROR;
    tcpClient.stop();
    return false;
  }
  // Settling the message can end the session on its own: the acknowledgement it owed may have gone
  // out only half way, leaving nothing for the packet the caller is about to build in this buffer.
  return this->connectionState == State::CONNECTED;
}

bool PubSubClient::pumpReader(uint32_t t) {
  if((rxPhase == RxPhase::Idle) && (tcpClient.available() == 0)) { return true; }
  if(rxPhase == RxPhase::Idle) {
    rxStartedMs = t;
    rxPhase = RxPhase::Header;
  }
  RxResult result = RxResult::Incomplete;
  if(rxPhase == RxPhase::Header) {
    result = advanceHeader();
    if((result == RxResult::Malformed) || (result == RxResult::TooLarge)) {
      connectionState = readFailureState(result);
      tcpClient.stop();
      resetReader();
      return false;
    }
  }
  if(rxPhase == RxPhase::Payload) {
    result = advancePayload();
  }
  if(result == RxResult::Complete) {
    lastInActivity = t;
    const uint16_t len = rxLen;
    const uint8_t llen = rxLengthLength;
    // Started over before the packet is answered: the callback may publish, and an outgoing packet
    // is built in this same buffer.
    resetReader();
    if(!dispatchPacket(len, llen)) {
      connectionState = State::PROTOCOL_ERROR;
      tcpClient.stop();
      return false;
    }
    // Answering the packet can end the session - an acknowledgement the link took only half of
    // leaves nothing to carry on with - and the caller is owed the session it has, not the one it
    // had a packet ago.
    return this->connectionState == State::CONNECTED;
  }
  // Half a packet is not an error yet - the rest may be one segment behind. It becomes one when it
  // stays away for the whole socket timeout: a peer that stops mid-packet is as gone as one that
  // stops answering, and the bytes already taken cannot be put back for a fresh start.
  if((t - rxStartedMs) >= (static_cast<uint32_t>(this->socketTimeout) * 1000U)) {
    connectionState = State::CONNECTION_TIMEOUT;
    tcpClient.stop();
    resetReader();
    return false;
  }
  return true;
}

void PubSubClient::keepAlivePing(uint32_t t) {
  // A client that would not take the ping has not pinged: counting it as sent would leave the
  // broker in silence for the rest of the interval and end the connection over a ping it never saw.
  lastPingAttempt = t;
  // Not in the packet buffer: the reader may be part way through a message there, and two bytes
  // written over its header would have the rest of it delivered as something else entirely.
  const uint8_t pingReq[2] = { MQTTPINGREQ, 0U };
  if(tcpClient.write(pingReq, 2U) == 2U) {
    lastOutActivity = lastInActivity = t;
    pingOutstanding = true;
    pingUnsent = false;
  } else if(!pingUnsent) {
    pingUnsent = true;
    if(refusedPings < UINT16_MAX) { refusedPings++; }
  } else {
    // A later refusal of the same ping; the run's deadline in loop() is what ends it.
  }
}

uint32_t PubSubClient::pingAnswerBudgetMs() const {
  const uint32_t keepAliveMs = static_cast<uint32_t>(this->keepAlive) * 1000U;
  const uint32_t pingIntervalMs = static_cast<uint32_t>(this->pingInterval) * 1000U;
  return ((keepAliveMs * brokerPatienceNumerator) / brokerPatienceDenominator) - pingIntervalMs;
}

bool PubSubClient::servicePing(uint32_t t) {
  // A keep-alive of zero is the broker being told not to time this client out, so there is nothing
  // to prove and no deadline to keep.
  if(this->keepAlive == 0U) { return true; }
  if(pingOutstanding || pingUnsent) {
    // One deadline covers the whole run, counted from when the ping fell due rather than from
    // whichever attempt is outstanding: a ping first refused and then taken would otherwise get a
    // second budget of its own, and the two together outlast what the broker waits through.
    if((t - pingDueSince) >= pingAnswerBudgetMs()) { return false; }
    // Bytes still waiting to be read may carry the answer, so a ping that has gone out is not
    // asked again until they have been. A ping the link would not take is a different matter:
    // what arrives says nothing about whether the link will take it now.
    const bool answerMayBeWaiting = pingOutstanding && (tcpClient.available() != 0);
    // A ping the client would not take is handed over again at once, because nothing of it has
    // left. One already on the wire waits longer: TCP is carrying it, so asking again only covers
    // a broker that let it go by.
    const uint32_t askAgainAfterMs = pingUnsent ? pingRetryIntervalMs : pingReaskIntervalMs;
    if(((t - lastPingAttempt) >= askAgainAfterMs) && !answerMayBeWaiting) {
      if(pingOutstanding && !pingReasked) {
        // Counted for the ping that was late, not for each ask after it.
        pingReasked = true;
        if(latePings < UINT16_MAX) { latePings++; }
      }
      keepAlivePing(t);
    }
    return true;
  }
  const uint32_t pingIntervalMs = static_cast<uint32_t>(this->pingInterval) * 1000U;
  if((t - lastInActivity > pingIntervalMs) || (t - lastOutActivity > pingIntervalMs)) {
    // Timed from when the ping fell due, not from the pass that noticed: a loop() held up
    // elsewhere would carry the whole budget past the point the broker stops waiting. The side
    // that went quiet first is the one being timed, and the ping being due puts it behind t.
    const uint32_t quietSince = ((t - lastInActivity) > (t - lastOutActivity)) ? lastInActivity : lastOutActivity;
    pingDueSince = quietSince + pingIntervalMs;
    pingReasked = false;
    keepAlivePing(t);
  }
  return true;
}

bool PubSubClient::loop() {
  if(connected()) {
    const uint32_t t = millis();
    if(!servicePing(t)) {
      this->connectionState = State::CONNECTION_TIMEOUT;
      tcpClient.stop();
      return false;
    }
    return pumpReader(t);
  }
  return false;
}

bool PubSubClient::publish(const char* topic, const char* payload, bool retained) {
  return publish(topic, reinterpret_cast<const uint8_t*>(payload), (payload != nullptr) ? strnlen(payload, this->bufferSize) : 0U, retained);
}

bool PubSubClient::publish(const char* topic, const uint8_t* payload, uint16_t plength, bool retained) {
  if((topic == nullptr) || !topicNameValid(topic)) {
    return false;
  }
  if(connected()) {
    if(!settleReader()) { return false; }
    if(this->bufferSize < MQTT_MAX_HEADER_SIZE + 2U + strnlen(topic, this->bufferSize) + plength) {
      // Too long
      return false;
    }
    // Leave room in the buffer for header and variable length field
    uint16_t length = MQTT_MAX_HEADER_SIZE;
    length = writeString(topic, this->buffer, length);

    // Add payload
    if(plength > 0U) {
      memcpy(this->buffer + length, payload, plength);
      length += plength;
    }

    const uint8_t header = static_cast<uint8_t>(MQTTPUBLISH | (retained ? 1U : 0U));
    return write(header, this->buffer, length - MQTT_MAX_HEADER_SIZE);
  }
  return false;
}

bool PubSubClient::publish_P(const char* topic, const char* payload, bool retained) {
  return publish_P(topic, reinterpret_cast<const uint8_t*>(payload), (payload != nullptr) ? strnlen(payload, this->bufferSize) : 0U, retained);
}

bool PubSubClient::publish_P(const char* topic, const uint8_t* payload, uint16_t plength, bool retained) {
  if((topic == nullptr) || !topicNameValid(topic)) {
    return false;
  }
  if(!connected()) {
    return false;
  }
  if(!settleReader()) { return false; }

  const uint16_t tlen = static_cast<uint16_t>(strnlen(topic, this->bufferSize));
  // The header and the topic go through the buffer; only the payload is streamed from flash, so
  // that is all the room needed here. Its length still has to fit the remaining-length field the
  // loop below builds, which is counted in a 16-bit number.
  if((this->bufferSize < MQTT_MAX_HEADER_SIZE + 2U + tlen) || (plength > (UINT16_MAX - 2U - tlen))) {
    return false;
  }

  const uint8_t header = static_cast<uint8_t>(MQTTPUBLISH | (retained ? 1U : 0U));
  uint16_t pos = 0U;
  this->buffer[pos++] = header;
  uint8_t llen = 0U;
  uint16_t len = static_cast<uint16_t>(plength + 2U + tlen);
  do {
    uint8_t digit = static_cast<uint8_t>(len & 127U);  // digit = len %128
    len >>= 7U;                                        // len = len / 128
    if(len > 0U) {
      digit |= 0x80U;
    }
    this->buffer[pos++] = digit;
    llen++;
  } while(len > 0U);

  pos = writeString(topic, this->buffer, pos);

  uint16_t sent = static_cast<uint16_t>(tcpClient.write(this->buffer, pos));
  bool taken = (sent == pos);
  uint16_t done = 0U;
  while(taken && (done < plength)) {
    // Copied out of flash a run at a time: the payload is not in the packet buffer, and handing
    // the link one byte per call costs a write down the whole TLS stack for each of them.
    uint8_t chunk[progmemChunkSize];
    const uint16_t piece = ((plength - done) < progmemChunkSize) ? static_cast<uint16_t>(plength - done) : progmemChunkSize;
    memcpy_P(chunk, payload + done, piece);
    const uint16_t rc = static_cast<uint16_t>(tcpClient.write(chunk, piece));
    done = static_cast<uint16_t>(done + rc);
    sent = static_cast<uint16_t>(sent + rc);
    taken = (rc == piece);
  }

  if(sent != 0U) { lastOutActivity = millis(); }

  const uint16_t expectedLength = static_cast<uint16_t>(1U + llen + 2U + tlen + plength);
  if((sent != 0U) && (sent != expectedLength)) {
    // Half a packet cannot be finished or taken back, exactly as for one built in the buffer.
    connectionState = State::CONNECTION_LOST;
    tcpClient.stop();
  }
  return (sent == expectedLength);
}

size_t PubSubClient::buildHeader(uint8_t header, uint8_t* buf, uint16_t length) {
  uint8_t lenBuf[4];
  size_t pos = 0U;
  uint16_t len = length;
  do {
    uint8_t digit = static_cast<uint8_t>(len & 127U);  // digit = len %128
    len = static_cast<uint16_t>(len >> 7U);            // len = len / 128
    if(len > 0U) {
      digit = static_cast<uint8_t>(digit | 0x80U);
    }
    lenBuf[pos++] = digit;
  } while(len > 0U);

  buf[MQTT_MAX_HEADER_SIZE - 1U - pos] = header;
  memcpy(buf + MQTT_MAX_HEADER_SIZE - pos, lenBuf, pos);
  return pos + 1U;  // Full header size is variable length bit plus the 1-byte fixed header
}

bool PubSubClient::write(uint8_t header, uint8_t* buf, uint16_t length) {
  const uint8_t hlen = static_cast<uint8_t>(buildHeader(header, buf, length));
  const uint16_t expected = static_cast<uint16_t>(length + hlen);
  uint8_t* const packet = buf + (MQTT_MAX_HEADER_SIZE - hlen);
#ifdef MQTT_MAX_TRANSFER_SIZE
  // A link that cannot take a whole packet in one call is told apart by this being set for it.
  const uint16_t chunkSize = static_cast<uint16_t>(MQTT_MAX_TRANSFER_SIZE);
#else
  const uint16_t chunkSize = expected;
#endif
  uint16_t sent = 0U;
  bool taken = true;
  while((sent < expected) && taken) {
    const uint16_t piece = ((expected - sent) < chunkSize) ? static_cast<uint16_t>(expected - sent) : chunkSize;
    const uint16_t rc = static_cast<uint16_t>(tcpClient.write(packet + sent, piece));
    sent = static_cast<uint16_t>(sent + rc);
    taken = (rc == piece);
  }
  // A link that took nothing has sent nothing: counting the attempt as outgoing traffic would put
  // the keep-alive ping off by another interval, and the broker gives up before that is over.
  if(sent != 0U) { lastOutActivity = millis(); }
  if((sent != 0U) && (sent != expected)) {
    // Half a packet cannot be finished later or taken back, and whatever goes out next is read as
    // the rest of it; the broker is left parsing a frame that never ends.
    connectionState = State::CONNECTION_LOST;
    tcpClient.stop();
  }
  return (sent == expected);
}

bool PubSubClient::subscribe(const char* topic, uint8_t qos) {
  if((topic == nullptr) || !topicFilterValid(topic)) {
    return false;
  }
  if(qos > 1U) {
    return false;
  }
  // Five bytes of header, two of packet id, two of filter length, the filter, and the qos byte
  // that follows it - which is the one the buffer has to have room for beyond the filter itself.
  if(this->bufferSize < 10U + strnlen(topic, this->bufferSize)) {
    // Too long
    return false;
  }
  if(connected()) {
    if(!settleReader()) { return false; }
    // Leave room in the buffer for header and variable length field
    uint16_t length = MQTT_MAX_HEADER_SIZE;
    if(++nextMsgId == 0U) {  // cppcheck-suppress knownConditionTrueFalse
      nextMsgId = 1U;
    }
    const uint16_t packetId = nextMsgId;
    this->buffer[length++] = static_cast<uint8_t>(nextMsgId >> 8U);
    this->buffer[length++] = static_cast<uint8_t>(nextMsgId & 0xFFU);
    length = writeString(topic, this->buffer, length);
    this->buffer[length++] = qos;
    if(!write(MQTTSUBSCRIBE | MQTTQOS1, this->buffer, length - MQTT_MAX_HEADER_SIZE)) {
      return false;
    }
    return awaitSubAck(packetId);
  }
  return false;
}

bool PubSubClient::unsubscribe(const char* topic) {
  if((topic == nullptr) || !topicFilterValid(topic)) {
    return false;
  }
  if(this->bufferSize < 9U + strnlen(topic, this->bufferSize)) {
    // Too long
    return false;
  }
  if(connected()) {
    if(!settleReader()) { return false; }
    uint16_t length = MQTT_MAX_HEADER_SIZE;
    if(++nextMsgId == 0U) {  // cppcheck-suppress knownConditionTrueFalse
      nextMsgId = 1U;
    }
    this->buffer[length++] = static_cast<uint8_t>(nextMsgId >> 8U);
    this->buffer[length++] = static_cast<uint8_t>(nextMsgId & 0xFFU);
    length = writeString(topic, this->buffer, length);
    return write(MQTTUNSUBSCRIBE | MQTTQOS1, this->buffer, length - MQTT_MAX_HEADER_SIZE);
  }
  return false;
}

void PubSubClient::disconnect() {
  const uint8_t disconnectPacket[2] = { MQTTDISCONNECT, 0U };
  (void)tcpClient.write(disconnectPacket, 2U);
  connectionState = State::DISCONNECTED;
  tcpClient.flush();
  tcpClient.stop();
  lastInActivity = lastOutActivity = millis();
}

uint16_t PubSubClient::writeString(const char* string, uint8_t* buf, uint16_t pos) {
  const uint16_t len = static_cast<uint16_t>(strlen(string));
  buf[pos++] = static_cast<uint8_t>(len >> 8U);
  buf[pos++] = static_cast<uint8_t>(len & 0xFFU);
  // No terminator: an MQTT string carries the two length bytes written above instead.
  memcpy(buf + pos, string, len);   // NOLINT(bugprone-not-null-terminated-result)
  return static_cast<uint16_t>(pos + len);
}

bool PubSubClient::connected() {
  if(!static_cast<bool>(tcpClient.connected())) {
    if(this->connectionState == State::CONNECTED) {
      this->connectionState = State::CONNECTION_LOST;
      tcpClient.flush();
      tcpClient.stop();
    }
    return false;
  }
  return this->connectionState == State::CONNECTED;
}

PubSubClient& PubSubClient::setServer(const IPAddress& ip, uint16_t port) {
  this->ip = ip;
  this->port = port;
  this->domain = nullptr;
  return *this;
}

PubSubClient& PubSubClient::setServer(const uint8_t* ip, uint16_t port) {
  IPAddress addr(ip[0], ip[1], ip[2], ip[3]);
  return setServer(addr, port);
}

PubSubClient& PubSubClient::setServer(const char* domain, uint16_t port) {
  this->domain = domain;
  this->port = port;
  return *this;
}

PubSubClient& PubSubClient::setCallback(MqttCallback callback) {
  this->callback = std::move(callback);
  return *this;
}

PubSubClient& PubSubClient::setKeepAlive(uint16_t keepAlive) {
  this->keepAlive = keepAlive;
  // Whichever order the two are set in, the ping stays inside what the broker was told to wait for.
  // The cap is applied to what was asked for rather than to the capped value, so a keep-alive
  // raised again gives the ping interval back instead of leaving it where a lower one pushed it.
  this->pingInterval = (this->wantedPingInterval > keepAlive) ? keepAlive : this->wantedPingInterval;
  return *this;
}

PubSubClient& PubSubClient::setPingInterval(uint16_t pingInterval) {
  this->wantedPingInterval = pingInterval;
  this->pingInterval = (pingInterval > this->keepAlive) ? this->keepAlive : pingInterval;
  return *this;
}

PubSubClient& PubSubClient::setSocketTimeout(uint16_t timeout) {
  this->socketTimeout = timeout;
  return *this;
}

PubSubClient::State PubSubClient::state() const {
  return this->connectionState;
}

uint16_t PubSubClient::getRefusedPingCount() const {
  return this->refusedPings;
}

uint16_t PubSubClient::getLatePingCount() const {
  return this->latePings;
}

bool PubSubClient::setBufferSize(uint16_t size) {
  if(size == 0U || size > defaultBufferSize) {
    return false;
  }
  this->bufferSize = size;
  return true;
}
