#pragma once

#include <Arduino.h>    /// Arduino core functions and types.
#include "IPAddress.h"  /// IP address representation.
#include "Client.h"     /// Abstract TCP client interface.
#include "Stream.h"     /// Abstract stream interface.

#define MQTT_VERSION_3_1 3    // NOLINT(modernize-macro-to-enum) — MQTT protocol version 3.1.
#define MQTT_VERSION_3_1_1 4  // NOLINT(modernize-macro-to-enum) — MQTT protocol version 3.1.1.

// MQTT_VERSION : Pick the version.
// #define MQTT_VERSION MQTT_VERSION_3_1
#ifndef MQTT_VERSION
#define MQTT_VERSION MQTT_VERSION_3_1_1
#endif

// MQTT_MAX_PACKET_SIZE : Maximum packet size. Override with setBufferSize().
#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 256
#endif

// MQTT_KEEPALIVE : Keep-alive interval in seconds. Override with setKeepAlive().
#ifndef MQTT_KEEPALIVE
#define MQTT_KEEPALIVE 15
#endif

// MQTT_SOCKET_TIMEOUT : Socket timeout interval in seconds. Override with setSocketTimeout().
#ifndef MQTT_SOCKET_TIMEOUT
#define MQTT_SOCKET_TIMEOUT 2
#endif

// MQTT_MAX_TRANSFER_SIZE : Limit data per network write call (needed for Arduino WiFi Shield).
//   Leave undefined to send the entire MQTT packet in a single write call.
// #define MQTT_MAX_TRANSFER_SIZE 80

/// @brief Lightweight MQTT client for embedded Arduino-compatible systems.
///
/// Supports MQTT 3.1 and 3.1.1, QoS 0 and 1, retain flags, Last Will, and
/// streaming large payloads via beginPublish() / write() / endPublish().
class PubSubClient final : public Print {
private:
  // clang-format off
  enum PacketType : uint8_t {
    MQTTCONNECT     = 1U << 4U,   // Client request to connect to Server.
    MQTTCONNACK     = 2U << 4U,   // Connect Acknowledgment.
    MQTTPUBLISH     = 3U << 4U,   // Publish message.
    MQTTPUBACK      = 4U << 4U,   // Publish Acknowledgment.
    MQTTPUBREC      = 5U << 4U,   // Publish Received (assured delivery part 1).
    MQTTPUBREL      = 6U << 4U,   // Publish Release (assured delivery part 2).
    MQTTPUBCOMP     = 7U << 4U,   // Publish Complete (assured delivery part 3).
    MQTTSUBSCRIBE   = 8U << 4U,   // Client Subscribe request.
    MQTTSUBACK      = 9U << 4U,   // Subscribe Acknowledgment.
    MQTTUNSUBSCRIBE = 10U << 4U,  // Client Unsubscribe request.
    MQTTUNSUBACK    = 11U << 4U,  // Unsubscribe Acknowledgment.
    MQTTPINGREQ     = 12U << 4U,  // PING Request.
    MQTTPINGRESP    = 13U << 4U,  // PING Response.
    MQTTDISCONNECT  = 14U << 4U,  // Client is Disconnecting.
    MQTTReserved    = 15U << 4U,  // Reserved.
  };
  enum Qos : uint8_t {
    MQTTQOS0 = 0U << 1U,  // Quality of Service level 0 — at most once.
    MQTTQOS1 = 1U << 1U,  // Quality of Service level 1 — at least once.
    MQTTQOS2 = 2U << 1U,  // Quality of Service level 2 — exactly once.
  };
  // clang-format on

  static constexpr uint8_t MQTT_MAX_HEADER_SIZE = 5U;                                           // Maximum MQTT fixed + variable header size in bytes.
  static constexpr uint16_t defaultBufferSize = static_cast<uint16_t>(MQTT_MAX_PACKET_SIZE);    // Default packet buffer size.
  static constexpr uint16_t defaultKeepAlive = static_cast<uint16_t>(MQTT_KEEPALIVE);           // Default keep-alive interval in seconds.
  static constexpr uint16_t defaultSocketTimeout = static_cast<uint16_t>(MQTT_SOCKET_TIMEOUT);  // Default socket timeout in seconds.

#if defined(ESP8266) || defined(ESP32)
#include <functional>
  using MqttCallback = std::function<void(char*, uint8_t*, uint32_t)>;  // Callback type for received MQTT messages (ESP).
#else
  using MqttCallback = void (*)(char*, uint8_t*, uint32_t);  // Callback type for received MQTT messages.
#endif

public:
  /// @brief MQTT connection state codes returned by state().
  // clang-format off
  enum class State : int8_t {
    CONNECTION_TIMEOUT      = -4,  // Server did not respond within socketTimeout.
    CONNECTION_LOST         = -3,  // TCP connection dropped unexpectedly.
    CONNECT_FAILED          = -2,  // TCP connection to broker failed.
    DISCONNECTED            = -1,  // Client is not connected.
    CONNECTED               =  0,  // Successfully connected to broker.
    CONNECT_BAD_PROTOCOL    =  1,  // Broker rejected unsupported protocol version.
    CONNECT_BAD_CLIENT_ID   =  2,  // Broker rejected the client identifier.
    CONNECT_UNAVAILABLE     =  3,  // Broker is unavailable.
    CONNECT_BAD_CREDENTIALS =  4,  // Invalid username or password.
    CONNECT_UNAUTHORIZED    =  5,  // Client is not authorized to connect.
  };
  // clang-format on

  /// @brief Constructs a default PubSubClient with no server or client configured.
  PubSubClient() = default;

  /// @brief Constructs a PubSubClient with a TCP client.
  /// @param client Reference to the TCP client used for the connection.
  explicit PubSubClient(Client& client);

  /// @brief Constructs a PubSubClient with a server IP address.
  /// @param addr Server IP address.
  /// @param port Server port number.
  /// @param client Reference to the TCP client.
  PubSubClient(IPAddress addr, uint16_t port, Client& client);

  /// @brief Constructs a PubSubClient with a server IP address and stream.
  /// @param addr Server IP address.
  /// @param port Server port number.
  /// @param client Reference to the TCP client.
  /// @param stream Reference to the stream for large payload passthrough.
  PubSubClient(IPAddress addr, uint16_t port, Client& client, Stream& stream);

  /// @brief Constructs a PubSubClient with a server IP address and callback.
  /// @param addr Server IP address.
  /// @param port Server port number.
  /// @param callback Callback invoked when a message is received.
  /// @param client Reference to the TCP client.
  PubSubClient(IPAddress addr, uint16_t port, MqttCallback callback, Client& client);

  /// @brief Constructs a PubSubClient with a server IP address, callback and stream.
  /// @param addr Server IP address.
  /// @param port Server port number.
  /// @param callback Callback invoked when a message is received.
  /// @param client Reference to the TCP client.
  /// @param stream Reference to the stream for large payload passthrough.
  PubSubClient(IPAddress addr, uint16_t port, MqttCallback callback, Client& client, Stream& stream);

  /// @brief Constructs a PubSubClient with a server IP given as a byte array.
  /// @param ip Pointer to a 4-byte array holding the server IP address.
  /// @param port Server port number.
  /// @param client Reference to the TCP client.
  PubSubClient(const uint8_t* ip, uint16_t port, Client& client);

  /// @brief Constructs a PubSubClient with a server IP byte array and stream.
  /// @param ip Pointer to a 4-byte array holding the server IP address.
  /// @param port Server port number.
  /// @param client Reference to the TCP client.
  /// @param stream Reference to the stream for large payload passthrough.
  PubSubClient(const uint8_t* ip, uint16_t port, Client& client, Stream& stream);

  /// @brief Constructs a PubSubClient with a server IP byte array and callback.
  /// @param ip Pointer to a 4-byte array holding the server IP address.
  /// @param port Server port number.
  /// @param callback Callback invoked when a message is received.
  /// @param client Reference to the TCP client.
  PubSubClient(const uint8_t* ip, uint16_t port, MqttCallback callback, Client& client);

  /// @brief Constructs a PubSubClient with a server IP byte array, callback and stream.
  /// @param ip Pointer to a 4-byte array holding the server IP address.
  /// @param port Server port number.
  /// @param callback Callback invoked when a message is received.
  /// @param client Reference to the TCP client.
  /// @param stream Reference to the stream for large payload passthrough.
  PubSubClient(const uint8_t* ip, uint16_t port, MqttCallback callback, Client& client, Stream& stream);

  /// @brief Constructs a PubSubClient with a server domain name.
  /// @param domain Null-terminated server domain name string.
  /// @param port Server port number.
  /// @param client Reference to the TCP client.
  PubSubClient(const char* domain, uint16_t port, Client& client);

  /// @brief Constructs a PubSubClient with a server domain name and stream.
  /// @param domain Null-terminated server domain name string.
  /// @param port Server port number.
  /// @param client Reference to the TCP client.
  /// @param stream Reference to the stream for large payload passthrough.
  PubSubClient(const char* domain, uint16_t port, Client& client, Stream& stream);

  /// @brief Constructs a PubSubClient with a server domain name and callback.
  /// @param domain Null-terminated server domain name string.
  /// @param port Server port number.
  /// @param callback Callback invoked when a message is received.
  /// @param client Reference to the TCP client.
  PubSubClient(const char* domain, uint16_t port, MqttCallback callback, Client& client);

  /// @brief Constructs a PubSubClient with a server domain name, callback and stream.
  /// @param domain Null-terminated server domain name string.
  /// @param port Server port number.
  /// @param callback Callback invoked when a message is received.
  /// @param client Reference to the TCP client.
  /// @param stream Reference to the stream for large payload passthrough.
  PubSubClient(const char* domain, uint16_t port, MqttCallback callback, Client& client, Stream& stream);

  /// @brief Default destructor.
  ~PubSubClient() = default;

  PubSubClient(const PubSubClient&) = delete;             // Delete copy constructor.
  PubSubClient& operator=(const PubSubClient&) = delete;  // Delete copy assignment operator.
  PubSubClient(PubSubClient&&) = delete;                  // Delete move constructor.
  PubSubClient& operator=(PubSubClient&&) = delete;       // Delete move assignment operator.

  /// @brief Sets the MQTT server by IP address.
  /// @param ip Server IP address.
  /// @param port Server port number.
  /// @return Reference to this instance for method chaining.
  PubSubClient& setServer(IPAddress ip, uint16_t port);

  /// @brief Sets the MQTT server by IP byte array.
  /// @param ip Pointer to a 4-byte array holding the server IP address.
  /// @param port Server port number.
  /// @return Reference to this instance for method chaining.
  PubSubClient& setServer(const uint8_t* ip, uint16_t port);

  /// @brief Sets the MQTT server by domain name.
  /// @param domain Null-terminated server domain name.
  /// @param port Server port number.
  /// @return Reference to this instance for method chaining.
  PubSubClient& setServer(const char* domain, uint16_t port);

  /// @brief Sets the callback invoked when an MQTT message is received.
  /// @param callback Function to call on message arrival.
  /// @return Reference to this instance for method chaining.
  PubSubClient& setCallback(MqttCallback callback);

  /// @brief Sets the TCP client used for the connection.
  /// @param client Reference to the TCP client.
  /// @return Reference to this instance for method chaining.
  PubSubClient& setClient(Client& client);

  /// @brief Sets the stream used for large payload passthrough.
  /// @param stream Reference to the stream.
  /// @return Reference to this instance for method chaining.
  PubSubClient& setStream(Stream& stream);

  /// @brief Sets the MQTT keep-alive interval.
  /// @param keepAlive Keep-alive interval in seconds.
  /// @return Reference to this instance for method chaining.
  PubSubClient& setKeepAlive(uint16_t keepAlive);

  /// @brief Sets the socket timeout interval.
  /// @param timeout Socket timeout in seconds.
  /// @return Reference to this instance for method chaining.
  PubSubClient& setSocketTimeout(uint16_t timeout);

  /// @brief Resizes the internal packet buffer.
  /// @param size New buffer size in bytes; must be between 1 and MQTT_MAX_PACKET_SIZE.
  /// @return `true` if the size is valid and was applied; otherwise `false`.
  [[nodiscard]] bool setBufferSize(uint16_t size);

  /// @brief Returns the current internal packet buffer size.
  /// @return Buffer size in bytes.
  [[nodiscard]] uint16_t getBufferSize() const;

  /// @brief Connects to the MQTT broker with the given client ID.
  /// @param id Null-terminated MQTT client identifier.
  /// @return `true` if the connection was established; otherwise `false`.
  [[nodiscard]] bool connect(const char* id);

  /// @brief Connects to the MQTT broker with credentials.
  /// @param id Null-terminated MQTT client identifier.
  /// @param user Null-terminated username; may be `nullptr`.
  /// @param pass Null-terminated password; may be `nullptr`.
  /// @return `true` if the connection was established; otherwise `false`.
  [[nodiscard]] bool connect(const char* id, const char* user, const char* pass);

  /// @brief Connects to the MQTT broker with a Last Will message.
  /// @param id Null-terminated MQTT client identifier.
  /// @param willTopic Null-terminated Last Will topic.
  /// @param willQos QoS level for the Last Will message (0 or 1).
  /// @param willRetain Whether the broker should retain the Last Will message.
  /// @param willMessage Null-terminated Last Will payload; may be `nullptr`.
  /// @return `true` if the connection was established; otherwise `false`.
  [[nodiscard]] bool connect(const char* id, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage);

  /// @brief Connects to the MQTT broker with credentials and a Last Will message.
  /// @param id Null-terminated MQTT client identifier.
  /// @param user Null-terminated username; may be `nullptr`.
  /// @param pass Null-terminated password; may be `nullptr`.
  /// @param willTopic Null-terminated Last Will topic.
  /// @param willQos QoS level for the Last Will message (0 or 1).
  /// @param willRetain Whether the broker should retain the Last Will message.
  /// @param willMessage Null-terminated Last Will payload; may be `nullptr`.
  /// @return `true` if the connection was established; otherwise `false`.
  [[nodiscard]] bool connect(const char* id, const char* user, const char* pass, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage);

  /// @brief Connects to the MQTT broker with full options.
  /// @param id Null-terminated MQTT client identifier.
  /// @param user Null-terminated username; may be `nullptr`.
  /// @param pass Null-terminated password; may be `nullptr`.
  /// @param willTopic Null-terminated Last Will topic; may be `nullptr` to disable.
  /// @param willQos QoS level for the Last Will message (0 or 1).
  /// @param willRetain Whether the broker should retain the Last Will message.
  /// @param willMessage Null-terminated Last Will payload; may be `nullptr`.
  /// @param cleanSession Whether to request a clean session from the broker.
  /// @return `true` if the connection was established; otherwise `false`.
  [[nodiscard]] bool connect(const char* id, const char* user, const char* pass, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage, bool cleanSession);

  /// @brief Sends an MQTT DISCONNECT packet and closes the TCP connection.
  void disconnect();

  /// @brief Publishes a string payload to a topic.
  /// @param topic Null-terminated MQTT topic.
  /// @param payload Null-terminated payload string; may be `nullptr` for an empty payload.
  /// @param retained Whether the broker should retain the message (default: `false`).
  /// @return `true` if the message was sent successfully; otherwise `false`.
  [[nodiscard]] bool publish(const char* topic, const char* payload, bool retained = false);

  /// @brief Publishes a binary payload to a topic.
  /// @param topic Null-terminated MQTT topic.
  /// @param payload Pointer to the payload buffer.
  /// @param plength Payload length in bytes.
  /// @param retained Whether the broker should retain the message (default: `false`).
  /// @return `true` if the message was sent successfully; otherwise `false`.
  [[nodiscard]] bool publish(const char* topic, const uint8_t* payload, uint16_t plength, bool retained = false);

  /// @brief Publishes a PROGMEM string payload to a topic.
  /// @param topic Null-terminated MQTT topic.
  /// @param payload Null-terminated PROGMEM string; may be `nullptr` for an empty payload.
  /// @param retained Whether the broker should retain the message.
  /// @return `true` if the message was sent successfully; otherwise `false`.
  [[nodiscard]] bool publish_P(const char* topic, const char* payload, bool retained);

  /// @brief Publishes a binary PROGMEM payload to a topic.
  /// @param topic Null-terminated MQTT topic.
  /// @param payload Pointer to PROGMEM payload buffer.
  /// @param plength Payload length in bytes.
  /// @param retained Whether the broker should retain the message.
  /// @return `true` if the message was sent successfully; otherwise `false`.
  [[nodiscard]] bool publish_P(const char* topic, const uint8_t* payload, uint16_t plength, bool retained);

  /// @brief Begins a streaming publish for payloads larger than the internal buffer.
  ///
  /// Use this API when the payload is too large to fit in the internal buffer at once:
  /// @code
  ///   beginPublish(topic, totalLength, retained);
  ///   write(data, dataLen);  // one or more calls
  ///   endPublish();
  /// @endcode
  /// @param topic Null-terminated MQTT topic.
  /// @param plength Total payload length in bytes.
  /// @param retained Whether the broker should retain the message.
  /// @return `true` if the MQTT header was sent successfully; otherwise `false`.
  [[nodiscard]] bool beginPublish(const char* topic, uint16_t plength, bool retained);

  /// @brief Finishes a streaming publish started with beginPublish().
  /// @return Always `true`.
  [[nodiscard]] bool endPublish();  // NOLINT(readability-convert-member-functions-to-static)

  /// @brief Writes a single payload byte (only valid between beginPublish() and endPublish()).
  /// @param data Byte to write.
  /// @return Number of bytes written.
  size_t write(uint8_t data) override;

  /// @brief Writes a block of payload bytes (only valid between beginPublish() and endPublish()).
  /// @param buffer Pointer to the data buffer.
  /// @param size Number of bytes to write.
  /// @return Number of bytes written.
  size_t write(const uint8_t* buffer, size_t size) override;

  /// @brief Subscribes to a topic.
  /// @param topic Null-terminated MQTT topic filter.
  /// @param qos QoS level (0 or 1; default: 0).
  /// @return `true` if the SUBSCRIBE packet was sent; otherwise `false`.
  [[nodiscard]] bool subscribe(const char* topic, uint8_t qos = 0U);

  /// @brief Unsubscribes from a topic.
  /// @param topic Null-terminated MQTT topic filter.
  /// @return `true` if the UNSUBSCRIBE packet was sent; otherwise `false`.
  [[nodiscard]] bool unsubscribe(const char* topic);

  /// @brief Processes incoming MQTT messages and maintains the keep-alive mechanism.
  ///        Must be called regularly from the application loop.
  /// @return `true` if the client is connected; `false` if the connection was lost.
  [[nodiscard]] bool loop();

  /// @brief Checks whether the client is currently connected to the broker.
  ///        Calls tcpClient->flush() and tcpClient->stop() if a dropped connection is detected.
  /// @return `true` if connected; otherwise `false`.
  [[nodiscard]] bool connected();  // NOLINT(readability-convert-member-functions-to-static)

  /// @brief Returns the current MQTT connection state.
  /// @return The connection state as a State enum value.
  [[nodiscard]] State state() const;

private:
  /// @brief Reads a single byte from the TCP client, blocking until data is available or timeout.
  /// @param result Pointer to the byte buffer to read into.
  /// @return `true` if a byte was read; `false` on timeout.
  bool readByte(uint8_t* result);  // NOLINT(readability-convert-member-functions-to-static)

  /// @brief Reads a single byte into result[*index] and increments index.
  /// @param result Pointer to the buffer.
  /// @param index Pointer to the current write index; incremented on success.
  /// @return `true` if a byte was read; `false` on timeout.
  bool readByte(uint8_t* result, uint16_t* index);  // NOLINT(readability-convert-member-functions-to-static)

  /// @brief Reads one complete MQTT packet into the internal buffer.
  /// @param lengthLength Output: set to the number of bytes in the variable-length field.
  /// @return Total number of bytes in the packet; 0 on error or oversized packet.
  uint32_t readPacket(uint8_t* lengthLength);

  /// @brief Sends a framed MQTT packet by prepending the fixed and variable-length header.
  /// @param header MQTT fixed-header byte.
  /// @param buf Buffer containing the payload, with MQTT_MAX_HEADER_SIZE bytes reserved at the start.
  /// @param length Payload length in bytes.
  /// @return `true` if all bytes were sent; otherwise `false`.
  bool write(uint8_t header, uint8_t* buf, uint16_t length);  // NOLINT(readability-convert-member-functions-to-static)

  /// @brief Checks whether a string fits in the remaining buffer space.
  ///        Calls tcpClient->stop() if the string does not fit.
  /// @param length Bytes already used in the buffer.
  /// @param str Null-terminated string to check.
  /// @return `true` if the string fits; otherwise `false`.
  bool checkStringLength(uint16_t length, const char* str);

  /// @brief Writes a length-prefixed MQTT string into a byte buffer.
  /// @param string Null-terminated source string.
  /// @param buf Destination buffer.
  /// @param pos Offset in buf to write to.
  /// @return New buffer position after the written string.
  static uint16_t writeString(const char* string, uint8_t* buf, uint16_t pos);

  /// @brief Builds the MQTT fixed + variable-length header in-place at the start of buf.
  /// @note The header occupies the last `returned_size` bytes of the MQTT_MAX_HEADER_SIZE-byte
  ///       prefix, i.e. starting at buf[MQTT_MAX_HEADER_SIZE - returned_size].
  /// @param header MQTT fixed-header byte.
  /// @param buf Buffer with MQTT_MAX_HEADER_SIZE bytes reserved at the front.
  /// @param length Payload length to encode in the variable-length field.
  /// @return Total header size (fixed byte + variable-length field bytes).
  size_t buildHeader(uint8_t header, uint8_t* buf, uint16_t length);

  Client* tcpClient = nullptr;                    // Pointer to the TCP client used for the connection.
  uint8_t buffer[defaultBufferSize]{};            // Internal packet buffer, zero-initialised.
  uint16_t bufferSize = defaultBufferSize;        // Active buffer size; may be reduced by setBufferSize().
  uint16_t keepAlive = defaultKeepAlive;          // Keep-alive interval in seconds.
  uint16_t socketTimeout = defaultSocketTimeout;  // Socket read timeout in seconds.
  uint16_t nextMsgId = 0U;                        // Next MQTT message ID (1–65535; 0 is reserved).
  uint32_t lastOutActivity = 0U;                  // Timestamp (ms) of the last outgoing packet.
  uint32_t lastInActivity = 0U;                   // Timestamp (ms) of the last incoming packet.
  bool pingOutstanding = false;                   // `true` if a PINGREQ was sent without a PINGRESP.
  MqttCallback callback = nullptr;                // User callback invoked on message receipt.
  IPAddress ip;                                   // Server IP address (used when domain is nullptr).
  const char* domain = nullptr;                   // Server domain name; takes priority over ip when set.
  uint16_t port = 0U;                             // Server port number.
  Stream* stream = nullptr;                       // Optional stream for large payload passthrough.
  State connectionState = State::DISCONNECTED;    // Current MQTT connection state.
};
