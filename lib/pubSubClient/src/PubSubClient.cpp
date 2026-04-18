/*

  PubSubClient.cpp - A simple client for MQTT.
  Nick O'Leary
  http://knolleary.net
*/

#include "PubSubClient.h"
#include "Arduino.h"


PubSubClient::PubSubClient(Client& client) {
  setClient(client);
}

PubSubClient::PubSubClient(IPAddress addr, uint16_t port, Client& client) {
  setServer(addr, port);
  setClient(client);
}
PubSubClient::PubSubClient(IPAddress addr, uint16_t port, Client& client, Stream& stream) {
  setServer(addr, port);
  setClient(client);
  setStream(stream);
}
PubSubClient::PubSubClient(IPAddress addr, uint16_t port, MqttCallback callback, Client& client) {
  setServer(addr, port);
  setCallback(callback);
  setClient(client);
}
PubSubClient::PubSubClient(IPAddress addr, uint16_t port, MqttCallback callback, Client& client, Stream& stream) {
  setServer(addr, port);
  setCallback(callback);
  setClient(client);
  setStream(stream);
}

PubSubClient::PubSubClient(const uint8_t* ip, uint16_t port, Client& client) {
  setServer(ip, port);
  setClient(client);
}
PubSubClient::PubSubClient(const uint8_t* ip, uint16_t port, Client& client, Stream& stream) {
  setServer(ip, port);
  setClient(client);
  setStream(stream);
}
PubSubClient::PubSubClient(const uint8_t* ip, uint16_t port, MqttCallback callback, Client& client) {
  setServer(ip, port);
  setCallback(callback);
  setClient(client);
}
PubSubClient::PubSubClient(const uint8_t* ip, uint16_t port, MqttCallback callback, Client& client, Stream& stream) {
  setServer(ip, port);
  setCallback(callback);
  setClient(client);
  setStream(stream);
}

PubSubClient::PubSubClient(const char* domain, uint16_t port, Client& client) {
  setServer(domain, port);
  setClient(client);
}
PubSubClient::PubSubClient(const char* domain, uint16_t port, Client& client, Stream& stream) {
  setServer(domain, port);
  setClient(client);
  setStream(stream);
}
PubSubClient::PubSubClient(const char* domain, uint16_t port, MqttCallback callback, Client& client) {
  setServer(domain, port);
  setCallback(callback);
  setClient(client);
}
PubSubClient::PubSubClient(const char* domain, uint16_t port, MqttCallback callback, Client& client, Stream& stream) {
  setServer(domain, port);
  setCallback(callback);
  setClient(client);
  setStream(stream);
}

bool PubSubClient::connect(const char* id) {  // NOLINT(readability-convert-member-functions-to-static)
  return connect(id, nullptr, nullptr, nullptr, 0U, false, nullptr, true);
}

bool PubSubClient::connect(const char* id, const char* user, const char* pass) {  // NOLINT(readability-convert-member-functions-to-static)
  return connect(id, user, pass, nullptr, 0U, false, nullptr, true);
}

bool PubSubClient::connect(const char* id, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage) {  // NOLINT(readability-convert-member-functions-to-static)
  return connect(id, nullptr, nullptr, willTopic, willQos, willRetain, willMessage, true);
}

bool PubSubClient::connect(const char* id, const char* user, const char* pass, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage) {  // NOLINT(readability-convert-member-functions-to-static)
  return connect(id, user, pass, willTopic, willQos, willRetain, willMessage, true);
}

bool PubSubClient::connect(const char* id, const char* user, const char* pass, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage, bool cleanSession) {  // NOLINT(readability-function-cognitive-complexity)
  if (!connected()) {
    bool result = false;

    if (_client->connected() != 0) {
      result = true;
    } else {
      if (domain != nullptr) {
        result = static_cast<bool>(_client->connect(this->domain, this->port));
      } else {
        result = static_cast<bool>(_client->connect(this->ip, this->port));
      }
    }

    if (result) {
      nextMsgId = 1U;
      // Leave room in the buffer for header and variable length field
      uint16_t length = MQTT_MAX_HEADER_SIZE;

#if MQTT_VERSION == MQTT_VERSION_3_1
      const uint8_t d[9] = {0x00U, 0x06U, 'M', 'Q', 'I', 's', 'd', 'p', MQTT_VERSION};
#elif MQTT_VERSION == MQTT_VERSION_3_1_1
      const uint8_t d[7] = {0x00U, 0x04U, 'M', 'Q', 'T', 'T', MQTT_VERSION};
#endif
      for (const uint8_t byte : d) {
        this->buffer[length++] = byte;
      }

      uint8_t v;
      if (willTopic != nullptr) {
        v = static_cast<uint8_t>(0x04U | (willQos << 3U) | (willRetain ? 0x20U : 0x00U));
      } else {
        v = 0x00U;
      }
      if (cleanSession) {
        v = v | 0x02U;
      }

      if (user != nullptr) {
        v = v | 0x80U;

        if (pass != nullptr) {
          v = v | (0x80U >> 1U);
        }
      }
      this->buffer[length++] = v;

      this->buffer[length++] = ((this->keepAlive) >> 8U);
      this->buffer[length++] = ((this->keepAlive) & 0xFFU);

      if (!checkStringLength(length, id)) { return false; }
      length = writeString(id, this->buffer, length);
      if (willTopic != nullptr) {
        if (!checkStringLength(length, willTopic)) { return false; }
        length = writeString(willTopic, this->buffer, length);
        if (!checkStringLength(length, willMessage)) { return false; }
        length = writeString(willMessage, this->buffer, length);
      }

      if (user != nullptr) {
        if (!checkStringLength(length, user)) { return false; }
        length = writeString(user, this->buffer, length);
        if (pass != nullptr) {
          if (!checkStringLength(length, pass)) { return false; }
          length = writeString(pass, this->buffer, length);
        }
      }

      write(MQTTCONNECT, this->buffer, length - MQTT_MAX_HEADER_SIZE);

      lastInActivity = lastOutActivity = millis();

      while (_client->available() == 0) {
        uint32_t t = millis();
        if (t - lastInActivity >= (static_cast<uint32_t>(this->socketTimeout) * 1000U)) {
          _state = MQTT_CONNECTION_TIMEOUT;
          _client->stop();
          return false;
        }
      }
      uint8_t llen;
      uint32_t len = readPacket(&llen);

      if (len == 4U) {
        if (buffer[3] == 0U) {
          lastInActivity = millis();
          pingOutstanding = false;
          _state = MQTT_CONNECTED;
          return true;
        }
        _state = static_cast<int8_t>(buffer[3]);
      }
      _client->stop();
    } else {
      _state = MQTT_CONNECT_FAILED;
    }
    return false;
  }
  return true;
}

bool PubSubClient::checkStringLength(uint16_t length, const char* str) {
  const bool fits = (length + 2U + strnlen(str, this->bufferSize) <= this->bufferSize);
  if (!fits) {
    _client->stop();
  }
  return fits;
}

// reads a byte into result
bool PubSubClient::readByte(uint8_t* result) {
  uint32_t previousMillis = millis();
  while (_client->available() == 0) {
    yield();
    uint32_t currentMillis = millis();
    if (currentMillis - previousMillis >= (static_cast<uint32_t>(this->socketTimeout) * 1000U)) {
      return false;
    }
  }
  *result = _client->read();
  return true;
}

// reads a byte into result[*index] and increments index
bool PubSubClient::readByte(uint8_t* result, uint16_t* index) {  // NOLINT(readability-convert-member-functions-to-static)
  uint16_t current_index = *index;
  uint8_t* write_address = &(result[current_index]);
  if (readByte(write_address)) {
    *index = current_index + 1U;
    return true;
  }
  return false;
}

uint32_t PubSubClient::readPacket(uint8_t* lengthLength) {  // NOLINT(readability-function-cognitive-complexity)
  uint16_t len = 0U;
  if (!readByte(this->buffer, &len)) {
    return 0U;
  }
  bool isPublish = (this->buffer[0] & 0xF0U) == MQTTPUBLISH;
  uint32_t multiplier = 1U;
  uint32_t length = 0U;
  uint8_t digit = 0U;
  uint16_t skip = 0U;
  uint32_t start = 0U;

  do {
    if (len == 5U) {
      // Invalid remaining length encoding - kill the connection
      _state = MQTT_DISCONNECTED;
      _client->stop();
      return 0U;
    }
    digit = 0U;
    if (!readByte(&digit)) {
      return 0U;
    }
    this->buffer[len++] = digit;
    length += (digit & 127U) * multiplier;
    multiplier <<= 7U;  // multiplier *= 128
  } while ((digit & 128U) != 0U);
  *lengthLength = static_cast<uint8_t>(len - 1U);

  if (isPublish) {
    // Read in topic length to calculate bytes to skip over for Stream writing
    if (!readByte(this->buffer, &len)) {
      return 0U;
    }
    if (!readByte(this->buffer, &len)) {
      return 0U;
    }
    skip = static_cast<uint16_t>((this->buffer[*lengthLength + 1U] << 8U) + this->buffer[*lengthLength + 2U]);
    start = 2U;
    if ((this->buffer[0] & MQTTQOS1) != 0U) {
      // skip message id
      skip += 2U;
    }
  }
  uint32_t idx = static_cast<uint32_t>(len);

  for (uint32_t i = start; i < length; i++) {
    uint8_t dataByte = 0U;
    if (!readByte(&dataByte)) {
      return 0U;
    }
    if (this->stream != nullptr) {
      if (isPublish && idx - *lengthLength - 2U > skip) {
        this->stream->write(dataByte);
      }
    }

    if (len < this->bufferSize) {
      this->buffer[len] = dataByte;
      len++;
    }
    idx++;
  }

  if (this->stream == nullptr && idx > this->bufferSize) {
    len = 0U;  // This will cause the packet to be ignored.
  }
  return len;
}

bool PubSubClient::loop() {  // NOLINT(readability-function-cognitive-complexity)
  if (connected()) {
    uint32_t t = millis();
    if ((t - lastInActivity > static_cast<uint32_t>(this->keepAlive) * 1000U) || (t - lastOutActivity > static_cast<uint32_t>(this->keepAlive) * 1000U)) {
      if (pingOutstanding) {
        this->_state = MQTT_CONNECTION_TIMEOUT;
        _client->stop();
        return false;
      }
      this->buffer[0] = MQTTPINGREQ;
      this->buffer[1] = 0U;
      _client->write(this->buffer, 2U);
      lastOutActivity = t;
      lastInActivity = t;
      pingOutstanding = true;
    }
    if (_client->available() != 0) {
      uint8_t llen;
      uint16_t len = readPacket(&llen);
      if (len > 0U) {
        lastInActivity = t;
        uint8_t type = this->buffer[0] & 0xF0U;
        if (type == MQTTPUBLISH) {
          if (callback != nullptr) {
            uint16_t tl = static_cast<uint16_t>((this->buffer[llen + 1U] << 8U) + this->buffer[llen + 2U]); /* topic length in bytes */
            memmove(this->buffer + llen + 2U, this->buffer + llen + 3U, tl);        /* move topic inside buffer 1 byte to front */
            this->buffer[llen + 2U + tl] = 0U;                                      /* end the topic as a 'C' string with \x00 */
            char* topic = reinterpret_cast<char*>(this->buffer + llen + 2U);
            // msgId only present for QOS>0
            if ((this->buffer[0] & 0x06U) == MQTTQOS1) {
              uint16_t msgId = static_cast<uint16_t>((this->buffer[llen + 3U + tl] << 8U) + this->buffer[llen + 3U + tl + 1U]);
              uint8_t* payload = this->buffer + llen + 3U + tl + 2U;
              callback(topic, payload, len - llen - 3U - tl - 2U);

              this->buffer[0] = MQTTPUBACK;
              this->buffer[1] = 2U;
              this->buffer[2] = static_cast<uint8_t>(msgId >> 8U);
              this->buffer[3] = static_cast<uint8_t>(msgId & 0xFFU);
              _client->write(this->buffer, 4U);
              lastOutActivity = t;

            } else {
              uint8_t* payload = this->buffer + llen + 3U + tl;
              callback(topic, payload, len - llen - 3U - tl);
            }
          }
        } else if (type == MQTTPINGREQ) {
          this->buffer[0] = MQTTPINGRESP;
          this->buffer[1] = 0U;
          _client->write(this->buffer, 2U);
        } else if (type == MQTTPINGRESP) {
          pingOutstanding = false;
        }
      } else if (!connected()) {
        // readPacket has closed the connection
        return false;
      }
    }
    return true;
  }
  return false;
}

bool PubSubClient::publish(const char* topic, const char* payload) {
  return publish(topic, reinterpret_cast<const uint8_t*>(payload), (payload != nullptr) ? strnlen(payload, this->bufferSize) : 0U, false);
}

bool PubSubClient::publish(const char* topic, const char* payload, bool retained) {
  return publish(topic, reinterpret_cast<const uint8_t*>(payload), (payload != nullptr) ? strnlen(payload, this->bufferSize) : 0U, retained);
}

bool PubSubClient::publish(const char* topic, const uint8_t* payload, uint16_t plength) {  // NOLINT(readability-convert-member-functions-to-static)
  return publish(topic, payload, plength, false);
}

bool PubSubClient::publish(const char* topic, const uint8_t* payload, uint16_t plength, bool retained) {
  if (connected()) {
    if (this->bufferSize < MQTT_MAX_HEADER_SIZE + 2U + strnlen(topic, this->bufferSize) + plength) {
      // Too long
      return false;
    }
    // Leave room in the buffer for header and variable length field
    uint16_t length = MQTT_MAX_HEADER_SIZE;
    length = writeString(topic, this->buffer, length);

    // Add payload
    for (uint16_t i = 0U; i < plength; i++) {
      this->buffer[length++] = payload[i];
    }

    // Write the header
    uint8_t header = MQTTPUBLISH;
    if (retained) {
      header |= 1U;
    }
    return write(header, this->buffer, length - MQTT_MAX_HEADER_SIZE);
  }
  return false;
}

bool PubSubClient::publish_P(const char* topic, const char* payload, bool retained) {
  return publish_P(topic, reinterpret_cast<const uint8_t*>(payload), (payload != nullptr) ? strnlen(payload, this->bufferSize) : 0U, retained);
}

bool PubSubClient::publish_P(const char* topic, const uint8_t* payload, uint16_t plength, bool retained) {
  uint8_t llen = 0U;
  uint16_t rc = 0U;
  uint16_t tlen;
  uint16_t pos = 0U;
  uint16_t expectedLength;

  if (!connected()) {
    return false;
  }

  tlen = static_cast<uint16_t>(strnlen(topic, this->bufferSize));

  uint8_t header = MQTTPUBLISH;
  if (retained) {
    header |= 1U;
  }
  this->buffer[pos++] = header;
  uint16_t len = static_cast<uint16_t>(plength + 2U + tlen);
  do {
    uint8_t digit = static_cast<uint8_t>(len & 127U);  // digit = len %128
    len >>= 7U;                                        // len = len / 128
    if (len > 0U) {
      digit |= 0x80U;
    }
    this->buffer[pos++] = digit;
    llen++;
  } while (len > 0U);

  pos = writeString(topic, this->buffer, pos);

  rc += static_cast<uint16_t>(_client->write(this->buffer, pos));

  for (uint16_t i = 0U; i < plength; i++) {
    rc += static_cast<uint16_t>(_client->write(pgm_read_byte_near(payload + i)));
  }

  lastOutActivity = millis();

  expectedLength = static_cast<uint16_t>(1U + llen + 2U + tlen + plength);

  return (rc == expectedLength);
}

bool PubSubClient::beginPublish(const char* topic, uint16_t plength, bool retained) {
  if (connected()) {
    // Send the header and variable length field
    uint16_t length = MQTT_MAX_HEADER_SIZE;
    length = writeString(topic, this->buffer, length);
    uint8_t header = MQTTPUBLISH;
    if (retained) {
      header |= 1U;
    }
    size_t hlen = buildHeader(header, this->buffer, plength + length - MQTT_MAX_HEADER_SIZE);
    uint16_t rc = _client->write(this->buffer + (MQTT_MAX_HEADER_SIZE - hlen), length - (MQTT_MAX_HEADER_SIZE - hlen));
    lastOutActivity = millis();
    return (rc == (length - (MQTT_MAX_HEADER_SIZE - hlen)));
  }
  return false;
}

bool PubSubClient::endPublish() {  // NOLINT(readability-convert-member-functions-to-static)
  return true;
}

size_t PubSubClient::write(uint8_t data) {
  lastOutActivity = millis();
  return _client->write(data);
}

size_t PubSubClient::write(const uint8_t* buffer, size_t size) {
  lastOutActivity = millis();
  return _client->write(buffer, size);
}

size_t PubSubClient::buildHeader(uint8_t header, uint8_t* buf, uint16_t length) {
  uint8_t lenBuf[4];
  size_t llen = 0U;
  size_t pos = 0U;
  uint16_t len = length;
  do {
    uint8_t digit = static_cast<uint8_t>(len & 127U);   // digit = len %128
    len = static_cast<uint16_t>(len >> 7U);              // len = len / 128
    if (len > 0U) {
      digit = static_cast<uint8_t>(digit | 0x80U);
    }
    lenBuf[pos++] = digit;
    llen++;
  } while (len > 0U);

  buf[MQTT_MAX_HEADER_SIZE - 1U - llen] = header;
  for (size_t i = 0U; i < llen; i++) {
    buf[MQTT_MAX_HEADER_SIZE - llen + i] = lenBuf[i];
  }
  return llen + 1U;  // Full header size is variable length bit plus the 1-byte fixed header
}

bool PubSubClient::write(uint8_t header, uint8_t* buf, uint16_t length) {  // NOLINT(readability-convert-member-functions-to-static)
  uint16_t rc;
  uint8_t hlen = buildHeader(header, buf, length);

#ifdef MQTT_MAX_TRANSFER_SIZE
  uint8_t* writeBuf = buf + (MQTT_MAX_HEADER_SIZE - hlen);
  uint16_t bytesRemaining = length + hlen;  // Match the length type
  uint8_t bytesToWrite;
  bool result = true;
  while ((bytesRemaining > 0U) && result) {
    bytesToWrite = (bytesRemaining > MQTT_MAX_TRANSFER_SIZE) ? MQTT_MAX_TRANSFER_SIZE : bytesRemaining;
    rc = _client->write(writeBuf, bytesToWrite);
    result = (rc == bytesToWrite);
    bytesRemaining -= rc;
    writeBuf += rc;
  }
  return result;
#else
  rc = _client->write(buf + (MQTT_MAX_HEADER_SIZE - hlen), length + hlen);
  lastOutActivity = millis();
  return (rc == hlen + length);
#endif
}

bool PubSubClient::subscribe(const char* topic) {
  return subscribe(topic, 0U);
}

bool PubSubClient::subscribe(const char* topic, uint8_t qos) {
  size_t topicLength = strnlen(topic, this->bufferSize);
  if (topic == nullptr) {
    return false;
  }
  if (qos > 1U) {
    return false;
  }
  if (this->bufferSize < 9U + topicLength) {
    // Too long
    return false;
  }
  if (connected()) {
    // Leave room in the buffer for header and variable length field
    uint16_t length = MQTT_MAX_HEADER_SIZE;
    nextMsgId++;
    if (nextMsgId == 0U) {
      nextMsgId = 1U;
    }
    this->buffer[length++] = static_cast<uint8_t>(nextMsgId >> 8U);
    this->buffer[length++] = static_cast<uint8_t>(nextMsgId & 0xFFU);
    length = writeString(topic, this->buffer, length);
    this->buffer[length++] = qos;
    return write(MQTTSUBSCRIBE | MQTTQOS1, this->buffer, length - MQTT_MAX_HEADER_SIZE);
  }
  return false;
}

bool PubSubClient::unsubscribe(const char* topic) {
  size_t topicLength = strnlen(topic, this->bufferSize);
  if (topic == nullptr) {
    return false;
  }
  if (this->bufferSize < 9U + topicLength) {
    // Too long
    return false;
  }
  if (connected()) {
    uint16_t length = MQTT_MAX_HEADER_SIZE;
    nextMsgId++;
    if (nextMsgId == 0U) {
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
  _client->write(this->buffer, 2U);
  _state = MQTT_DISCONNECTED;
  _client->flush();
  _client->stop();
  lastInActivity = lastOutActivity = millis();
}

uint16_t PubSubClient::writeString(const char* string, uint8_t* buf, uint16_t pos) {
  const char* idp = string;
  uint16_t i = 0U;
  pos += 2U;
  while (*idp != '\0') {
    buf[pos++] = *idp++;
    i++;
  }
  buf[pos - i - 2U] = static_cast<uint8_t>(i >> 8U);
  buf[pos - i - 1U] = static_cast<uint8_t>(i & 0xFFU);
  return pos;
}

bool PubSubClient::connected() {
  bool rc;
  if (_client == nullptr) {
    rc = false;
  } else {
    rc = static_cast<bool>(_client->connected());
    if (!rc) {
      if (this->_state == MQTT_CONNECTED) {
        this->_state = MQTT_CONNECTION_LOST;
        _client->flush();
        _client->stop();
      }
    } else {
      return this->_state == MQTT_CONNECTED;
    }
  }
  return rc;
}

PubSubClient& PubSubClient::setServer(const uint8_t* ip, uint16_t port) {  // NOLINT(readability-convert-member-functions-to-static)
  IPAddress addr(ip[0], ip[1], ip[2], ip[3]);
  return setServer(addr, port);
}

PubSubClient& PubSubClient::setServer(IPAddress ip, uint16_t port) {
  this->ip = ip;
  this->port = port;
  this->domain = nullptr;
  return *this;
}

PubSubClient& PubSubClient::setServer(const char* domain, uint16_t port) {
  this->domain = domain;
  this->port = port;
  return *this;
}

PubSubClient& PubSubClient::setCallback(MqttCallback callback) {
  this->callback = callback;
  return *this;
}

PubSubClient& PubSubClient::setClient(Client& client) {
  this->_client = &client;
  return *this;
}

PubSubClient& PubSubClient::setStream(Stream& stream) {
  this->stream = &stream;
  return *this;
}

int8_t PubSubClient::state() const {
  return this->_state;
}

bool PubSubClient::setBufferSize(uint16_t size) {
  if (size == 0U || size > MQTT_MAX_PACKET_SIZE) {
    return false;
  }
  this->bufferSize = size;
  return true;
}

uint16_t PubSubClient::getBufferSize() const {
  return this->bufferSize;
}
PubSubClient& PubSubClient::setKeepAlive(uint16_t keepAlive) {
  this->keepAlive = keepAlive;
  return *this;
}
PubSubClient& PubSubClient::setSocketTimeout(uint16_t timeout) {
  this->socketTimeout = timeout;
  return *this;
}
