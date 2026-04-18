#pragma once

#include <Arduino.h>
#include "IPAddress.h"
#include "Client.h"
#include "Stream.h"

#define MQTT_VERSION_3_1 3    // NOLINT(modernize-macro-to-enum)
#define MQTT_VERSION_3_1_1 4  // NOLINT(modernize-macro-to-enum)

// MQTT_VERSION : Pick the version
// #define MQTT_VERSION MQTT_VERSION_3_1
#ifndef MQTT_VERSION
#define MQTT_VERSION MQTT_VERSION_3_1_1
#endif

// MQTT_MAX_PACKET_SIZE : Maximum packet size. Override with setBufferSize().
#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 256
#endif

// MQTT_KEEPALIVE : keepAlive interval in Seconds. Override with setKeepAlive()
#ifndef MQTT_KEEPALIVE
#define MQTT_KEEPALIVE 15
#endif

// MQTT_SOCKET_TIMEOUT: socket timeout interval in Seconds. Override with setSocketTimeout()
#ifndef MQTT_SOCKET_TIMEOUT
#define MQTT_SOCKET_TIMEOUT 15
#endif

// MQTT_MAX_TRANSFER_SIZE : limit how much data is passed to the network client
//  in each write call. Needed for the Arduino Wifi Shield. Leave undefined to
//  pass the entire MQTT packet in each write call.
// #define MQTT_MAX_TRANSFER_SIZE 80

class PubSubClient final : public Print {
private:
  // clang-format off
  static inline constexpr uint8_t MQTTCONNECT     = 1U << 4U;  // Client request to connect to Server
  static inline constexpr uint8_t MQTTCONNACK     = 2U << 4U;  // Connect Acknowledgment
  static inline constexpr uint8_t MQTTPUBLISH     = 3U << 4U;  // Publish message
  static inline constexpr uint8_t MQTTPUBACK      = 4U << 4U;  // Publish Acknowledgment
  static inline constexpr uint8_t MQTTPUBREC      = 5U << 4U;  // Publish Received (assured delivery part 1)
  static inline constexpr uint8_t MQTTPUBREL      = 6U << 4U;  // Publish Release (assured delivery part 2)
  static inline constexpr uint8_t MQTTPUBCOMP     = 7U << 4U;  // Publish Complete (assured delivery part 3)
  static inline constexpr uint8_t MQTTSUBSCRIBE   = 8U << 4U;  // Client Subscribe request
  static inline constexpr uint8_t MQTTSUBACK      = 9U << 4U;  // Subscribe Acknowledgment
  static inline constexpr uint8_t MQTTUNSUBSCRIBE = 10U << 4U; // Client Unsubscribe request
  static inline constexpr uint8_t MQTTUNSUBACK    = 11U << 4U; // Unsubscribe Acknowledgment
  static inline constexpr uint8_t MQTTPINGREQ     = 12U << 4U; // PING Request
  static inline constexpr uint8_t MQTTPINGRESP    = 13U << 4U; // PING Response
  static inline constexpr uint8_t MQTTDISCONNECT  = 14U << 4U; // Client is Disconnecting
  static inline constexpr uint8_t MQTTReserved    = 15U << 4U; // Reserved
  // clang-format on

  static inline constexpr uint8_t MQTTQOS0 = (0U << 1U);
  static inline constexpr uint8_t MQTTQOS1 = (1U << 1U);
  static inline constexpr uint8_t MQTTQOS2 = (2U << 1U);

  // Maximum size of fixed header and variable length size header
  static inline constexpr uint8_t MQTT_MAX_HEADER_SIZE = 5U;

#if defined(ESP8266) || defined(ESP32)
#include <functional>
  using MqttCallback = std::function<void(char*, uint8_t*, unsigned int)>;
#else
  using MqttCallback = void (*)(char*, uint8_t*, unsigned int);
#endif

public:
  // Possible values for client.state()
  // clang-format off
  static inline constexpr int8_t MQTT_CONNECTION_TIMEOUT     = -4;
  static inline constexpr int8_t MQTT_CONNECTION_LOST        = -3;
  static inline constexpr int8_t MQTT_CONNECT_FAILED         = -2;
  static inline constexpr int8_t MQTT_DISCONNECTED           = -1;
  static inline constexpr int8_t MQTT_CONNECTED               = 0;
  static inline constexpr int8_t MQTT_CONNECT_BAD_PROTOCOL    = 1;
  static inline constexpr int8_t MQTT_CONNECT_BAD_CLIENT_ID   = 2;
  static inline constexpr int8_t MQTT_CONNECT_UNAVAILABLE     = 3;
  static inline constexpr int8_t MQTT_CONNECT_BAD_CREDENTIALS = 4;
  static inline constexpr int8_t MQTT_CONNECT_UNAUTHORIZED    = 5;
  // clang-format on

  PubSubClient() = default;
  PubSubClient(Client& client);
  PubSubClient(IPAddress, uint16_t, Client& client);
  PubSubClient(IPAddress, uint16_t, Client& client, Stream&);
  PubSubClient(IPAddress, uint16_t, MqttCallback callback, Client& client);
  PubSubClient(IPAddress, uint16_t, MqttCallback callback, Client& client, Stream&);
  PubSubClient(const uint8_t*, uint16_t, Client& client);
  PubSubClient(const uint8_t*, uint16_t, Client& client, Stream&);
  PubSubClient(const uint8_t*, uint16_t, MqttCallback callback, Client& client);
  PubSubClient(const uint8_t*, uint16_t, MqttCallback callback, Client& client, Stream&);
  PubSubClient(const char*, uint16_t, Client& client);
  PubSubClient(const char*, uint16_t, Client& client, Stream&);
  PubSubClient(const char*, uint16_t, MqttCallback callback, Client& client);
  PubSubClient(const char*, uint16_t, MqttCallback callback, Client& client, Stream&);
  ~PubSubClient() = default;

  PubSubClient& setServer(IPAddress ip, uint16_t port);
  PubSubClient& setServer(const uint8_t* ip, uint16_t port);
  PubSubClient& setServer(const char* domain, uint16_t port);
  PubSubClient& setCallback(MqttCallback callback);
  PubSubClient& setClient(Client& client);
  PubSubClient& setStream(Stream& stream);
  PubSubClient& setKeepAlive(uint16_t keepAlive);
  PubSubClient& setSocketTimeout(uint16_t timeout);

  bool setBufferSize(uint16_t size);
  [[nodiscard]] uint16_t getBufferSize() const;

  bool connect(const char* id);
  bool connect(const char* id, const char* user, const char* pass);
  bool connect(const char* id, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage);
  bool connect(const char* id, const char* user, const char* pass, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage);
  bool connect(const char* id, const char* user, const char* pass, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage, bool cleanSession);
  void disconnect();
  bool publish(const char* topic, const char* payload);
  bool publish(const char* topic, const char* payload, bool retained);
  bool publish(const char* topic, const uint8_t* payload, uint16_t plength);
  bool publish(const char* topic, const uint8_t* payload, uint16_t plength, bool retained);
  bool publish_P(const char* topic, const char* payload, bool retained);
  bool publish_P(const char* topic, const uint8_t* payload, uint16_t plength, bool retained);
  // Start to publish a message.
  // This API:
  //   beginPublish(...)
  //   one or more calls to write(...)
  //   endPublish()
  // Allows for arbitrarily large payloads to be sent without them having to be copied into
  // a new buffer and held in memory at one time
  // Returns 1 if the message was started successfully, 0 if there was an error
  bool beginPublish(const char* topic, uint16_t plength, bool retained);
  // Finish off this publish message (started with beginPublish)
  // Returns 1 if the packet was sent successfully, 0 if there was an error
  bool endPublish();
  // Write a single byte of payload (only to be used with beginPublish/endPublish)
  size_t write(uint8_t) override;
  // Write size bytes from buffer into the payload (only to be used with beginPublish/endPublish)
  // Returns the number of bytes written
  size_t write(const uint8_t* buffer, size_t size) override;
  bool subscribe(const char* topic);
  bool subscribe(const char* topic, uint8_t qos);
  bool unsubscribe(const char* topic);
  bool loop();
  bool connected();
  [[nodiscard]] int8_t state() const;

private:
  Client* _client = nullptr;
  uint8_t buffer[MQTT_MAX_PACKET_SIZE];
  uint16_t bufferSize = MQTT_MAX_PACKET_SIZE;
  uint16_t keepAlive = MQTT_KEEPALIVE;
  uint16_t socketTimeout = MQTT_SOCKET_TIMEOUT;
  uint16_t nextMsgId = 0U;
  uint32_t lastOutActivity = 0U;
  uint32_t lastInActivity = 0U;
  bool pingOutstanding = false;
  MqttCallback callback = nullptr;
  uint32_t readPacket(uint8_t*);
  bool readByte(uint8_t* result);
  bool readByte(uint8_t* result, uint16_t* index);
  bool write(uint8_t header, uint8_t* buf, uint16_t length);
  bool checkStringLength(uint16_t length, const char* str);
  static uint16_t writeString(const char* string, uint8_t* buf, uint16_t pos);
  // Build up the header ready to send
  // Returns the size of the header
  // Note: the header is built at the end of the first MQTT_MAX_HEADER_SIZE bytes, so will start
  //       (MQTT_MAX_HEADER_SIZE - <returned size>) bytes into the buffer
  size_t buildHeader(uint8_t header, uint8_t* buf, uint16_t length);
  IPAddress ip;
  const char* domain = nullptr;
  uint16_t port = 0U;
  Stream* stream = nullptr;
  int8_t _state = MQTT_DISCONNECTED;
};
