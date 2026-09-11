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
  if(connected()) { return true; }
  const bool result = (tcpClient.connected() != 0) ||
                      static_cast<bool>(domain != nullptr ? tcpClient.connect(this->domain, this->port)
                                                          : tcpClient.connect(this->ip, this->port));
  if(!result) {
    connectionState = State::CONNECT_FAILED;
    tcpClient.stop();
    return false;
  }
  nextMsgId = 1U;
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
  const bool connAckSized = (connAck == RxResult::Complete) && (rxLen == 4U);
  const uint8_t connAckCode = connAckSized ? this->buffer[3] : 0xFFU;
  // The reader has to start clean for the session: whatever it kept about the CONNACK would
  // otherwise be finished a second time on the first loop(), before any real packet is read.
  resetReader();

  if(connAckSized) {
    if(connAckCode == 0U) {
      lastInActivity = millis();
      pingOutstanding = false;
      // A ping the last session's client would not take belongs to that session. Left standing,
      // its deadline is already past, so the first ping this one cannot hand over ends the
      // connection on the spot instead of being retried.
      pingUnsent = false;
      connectionState = State::CONNECTED;
      return true;
    }
    connectionState = static_cast<State>(connAckCode);
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
      connectionState = State::CONNECTION_TIMEOUT;
      tcpClient.stop();
      waiting = false;
    } else if(isSubAckFor(packetId)) {
      // One filter goes out per SUBSCRIBE, so the first return code is the one that answers it.
      granted = (this->buffer[rxLengthLength + 3U] != subscribeFailureCode);
      waiting = false;
    } else {
      // The broker got a word in first; it is this session's traffic and is answered as such.
      dispatchPacket(millis());
      waiting = ((millis() - startMs) < timeoutMs);
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
  rxOversized = false;
}

PubSubClient::RxResult PubSubClient::readPacketBlocking() {
  const uint32_t timeoutMs = static_cast<uint32_t>(this->socketTimeout) * 1000U;
  const uint32_t startMs = millis();
  resetReader();
  while(true) {
    if(rxPhase == RxPhase::Payload) {
      if(advancePayload() == RxResult::Complete) { return RxResult::Complete; }
    } else if(advanceHeader() == RxResult::Malformed) {
      return RxResult::Malformed;
    } else {
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
      // The topic-length field is two bytes, and the remaining length counts them. A PUBLISH that
      // announces fewer has none to give: the payload length derived from it would wrap to nearly
      // 4 GB. Malformed the same way an invalid remaining length is, and dropped the same way.
      const bool isPublish = ((this->buffer[0] & 0xF0U) == MQTTPUBLISH);
      if(isPublish && (rxRemaining < 2U)) { return RxResult::Malformed; }
      // Marked here, acted on as the payload arrives: it is taken off the socket either way.
      rxOversized = (rxLen + rxRemaining) > this->bufferSize;
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
    takePayloadBulk((static_cast<uint32_t>(ready) < left) ? static_cast<uint32_t>(ready) : left);
  }
  return RxResult::Complete;
}

void PubSubClient::takePayloadBulk(uint32_t take) {
  const uint32_t room = (rxLen < this->bufferSize) ? static_cast<uint32_t>(this->bufferSize - rxLen) : 0U;
  const uint32_t kept = (room < take) ? room : take;
  uint32_t stored = 0U;
  if(kept != 0U) {
    // What the client hands over is what was taken: counting the request instead would walk the
    // parse position past bytes still on the socket, and every packet after it would be misread.
    const int got = tcpClient.read(&this->buffer[rxLen], kept);
    stored = (got > 0) ? static_cast<uint32_t>(got) : 0U;
    rxLen = static_cast<uint16_t>(rxLen + stored);
    rxPayloadDone += stored;
  }
  if(stored < kept) { return; }   // Short read: the rest is still coming, so nothing to discard yet.
  // What will not fit is still taken off the socket: left there, it would be read as the next
  // packet's header.
  uint32_t dropped = 0U;
  while(dropped < (take - kept)) {
    uint8_t discard[discardChunkSize];
    const uint32_t want = ((take - kept - dropped) < discardChunkSize) ? (take - kept - dropped) : discardChunkSize;
    const int got = tcpClient.read(discard, want);
    if(got <= 0) { break; }
    dropped += static_cast<uint32_t>(got);
  }
  rxPayloadDone += dropped;
}

void PubSubClient::dispatchPacket(uint32_t t) {
  const uint16_t len = rxLen;
  const uint8_t llen = rxLengthLength;
  {
    const uint8_t type = this->buffer[0] & 0xF0U;
    if(type == MQTTPUBLISH) {
      if(callback != nullptr) {
        const uint16_t tl = static_cast<uint16_t>((this->buffer[llen + 1U] << 8U) + this->buffer[llen + 2U]); /* topic length in bytes */
        // The topic length and the packet length are two independent numbers off the wire, and
        // every index below is built from the first one. A packet where they disagree is dropped
        // rather than trusted: the reader consumed exactly the announced bytes, so the stream
        // stays in step and only this message is lost.
        const uint16_t msgIdLen = ((this->buffer[0] & 0x06U) == MQTTQOS1) ? 2U : 0U;
        if(len < (static_cast<uint32_t>(llen) + 3U + tl + msgIdLen)) {
          return;
        }
        memmove(this->buffer + llen + 2U, this->buffer + llen + 3U, tl);                                      /* move topic inside buffer 1 byte to front */
        this->buffer[llen + 2U + tl] = 0U;                                                                    /* end the topic as a 'C' string with \x00 */
        char* const topic = reinterpret_cast<char*>(this->buffer + llen + 2U);
        // msgId only present for QOS>0
        if((this->buffer[0] & 0x06U) == MQTTQOS1) {
          const uint16_t msgId = static_cast<uint16_t>((this->buffer[llen + 3U + tl] << 8U) + this->buffer[llen + 3U + tl + 1U]);
          uint8_t* const payload = this->buffer + llen + 3U + tl + 2U;
          callback(topic, payload, len - llen - 3U - tl - 2U);

          this->buffer[0] = MQTTPUBACK;
          this->buffer[1] = 2U;
          this->buffer[2] = static_cast<uint8_t>(msgId >> 8U);
          this->buffer[3] = static_cast<uint8_t>(msgId & 0xFFU);
          tcpClient.write(this->buffer, 4U);
          lastOutActivity = t;

        } else {
          uint8_t* const payload = this->buffer + llen + 3U + tl;
          callback(topic, payload, len - llen - 3U - tl);
        }
      }
    } else if(type == MQTTPINGREQ) {
      this->buffer[0] = MQTTPINGRESP;
      this->buffer[1] = 0U;
      tcpClient.write(this->buffer, 2U);
    } else if(type == MQTTPINGRESP) {
      pingOutstanding = false;
    }
  }
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
    if(result == RxResult::Malformed) {
      connectionState = State::DISCONNECTED;
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
    // An oversized packet was taken off the socket to keep the stream in step, and goes no further.
    if(!rxOversized) { dispatchPacket(t); }
    resetReader();
    return true;
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

bool PubSubClient::keepAlivePing(uint32_t t) {
  const uint32_t keepAliveMs = static_cast<uint32_t>(this->keepAlive) * 1000U;
  // A client that would not take the ping has not pinged: counting it as sent would leave the
  // broker in silence for the rest of the interval and end the connection over a ping it never
  // saw. The timers stay put so a later pass asks again - a second apart, because loop() runs at
  // the caller's pass rate and one that just refused will not take it a millisecond later.
  const bool firstAttempt = !pingUnsent;
  if(firstAttempt || ((t - lastPingAttempt) >= pingRetryIntervalMs)) {
    lastPingAttempt = t;
    this->buffer[0] = MQTTPINGREQ;
    this->buffer[1] = 0U;
    if(tcpClient.write(this->buffer, 2U) == 2U) {
      lastOutActivity = lastInActivity = t;
      pingOutstanding = true;
      pingUnsent = false;
    } else if(firstAttempt) {
      pingUnsent = true;
      pingUnsentSince = t;
      if(refusedPings < UINT16_MAX) { refusedPings++; }
    } else {
      // A later refusal of the same ping; the deadline below is what ends it.
    }
  }
  // A client that never takes it is as dead as a broker that never answers.
  return !pingUnsent || ((t - pingUnsentSince) <= keepAliveMs);
}

bool PubSubClient::loop() {
  if(connected()) {
    const uint32_t t = millis();
    const uint32_t keepAliveMs = static_cast<uint32_t>(this->keepAlive) * 1000U;
    if((t - lastInActivity > keepAliveMs) || (t - lastOutActivity > keepAliveMs)) {
      if(pingOutstanding || !keepAlivePing(t)) {
        this->connectionState = State::CONNECTION_TIMEOUT;
        tcpClient.stop();
        return false;
      }
    }
    return pumpReader(t);
  }
  return false;
}

bool PubSubClient::publish(const char* topic, const char* payload, bool retained) {
  return publish(topic, reinterpret_cast<const uint8_t*>(payload), (payload != nullptr) ? strnlen(payload, this->bufferSize) : 0U, retained);
}

bool PubSubClient::publish(const char* topic, const uint8_t* payload, uint16_t plength, bool retained) {
  if(connected()) {
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
  if(!connected()) {
    return false;
  }

  const uint16_t tlen = static_cast<uint16_t>(strnlen(topic, this->bufferSize));

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

  uint16_t rc = static_cast<uint16_t>(tcpClient.write(this->buffer, pos));
  for(uint16_t i = 0U; i < plength; i++) {
    rc += static_cast<uint16_t>(tcpClient.write(pgm_read_byte_near(payload + i)));
  }

  lastOutActivity = millis();

  const uint16_t expectedLength = static_cast<uint16_t>(1U + llen + 2U + tlen + plength);
  return (rc == expectedLength);
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

#ifdef MQTT_MAX_TRANSFER_SIZE
  uint8_t* writeBuf = buf + (MQTT_MAX_HEADER_SIZE - hlen);
  uint16_t bytesRemaining = length + hlen;  // Match the length type
  bool result = true;
  while((bytesRemaining > 0U) && result) {
    const uint8_t bytesToWrite = (bytesRemaining > MQTT_MAX_TRANSFER_SIZE) ? MQTT_MAX_TRANSFER_SIZE : bytesRemaining;
    const uint16_t rc = tcpClient.write(writeBuf, bytesToWrite);
    result = (rc == bytesToWrite);
    bytesRemaining -= rc;
    writeBuf += rc;
  }
  return result;
#else
  const uint16_t rc = tcpClient.write(buf + (MQTT_MAX_HEADER_SIZE - hlen), length + hlen);
  lastOutActivity = millis();
  return (rc == hlen + length);
#endif
}

bool PubSubClient::subscribe(const char* topic, uint8_t qos) {
  if(topic == nullptr) {
    return false;
  }
  if(qos > 1U) {
    return false;
  }
  if(this->bufferSize < 9U + strnlen(topic, this->bufferSize)) {
    // Too long
    return false;
  }
  if(connected()) {
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
  if(topic == nullptr) {
    return false;
  }
  if(this->bufferSize < 9U + strnlen(topic, this->bufferSize)) {
    // Too long
    return false;
  }
  if(connected()) {
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
  this->buffer[0] = MQTTDISCONNECT;
  this->buffer[1] = 0U;
  tcpClient.write(this->buffer, 2U);
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

bool PubSubClient::setBufferSize(uint16_t size) {
  if(size == 0U || size > defaultBufferSize) {
    return false;
  }
  this->bufferSize = size;
  return true;
}
