#pragma once

#include <Arduino.h>    /// Arduino core functions and types.
#include "IPAddress.h"  /// IP address representation.
#include "Client.h"     /// Abstract TCP client interface.
#if defined(ESP8266) || defined(ESP32)
#include <functional>   /// std::function for the message callback.
#endif

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
/// Supports MQTT 3.1 and 3.1.1, retain flags and Last Will. Publishing is QoS 0; an inbound
/// QoS 1 PUBLISH is acknowledged, and a subscription may ask for QoS 0 or 1.
class PubSubClient final {
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
  static constexpr uint32_t pingRetryIntervalMs = 1000U;                                        // Least time between two attempts to hand the same PINGREQ over.
  static constexpr uint8_t subscribeFailureCode = 0x80U;                                        // SUBACK return code for a filter the broker would not grant.
  static constexpr uint8_t highestNamedConnAckCode = 5U;                                        // Largest CONNACK return code the State enum has a name for.

#if defined(ESP8266) || defined(ESP32)
  using MqttCallback = std::function<void(char*, uint8_t*, uint32_t)>;  // Callback type for received MQTT messages (ESP).
#else
  using MqttCallback = void (*)(char*, uint8_t*, uint32_t);  // Callback type for received MQTT messages.
#endif

public:
  /// @brief MQTT connection state codes returned by state().
  // clang-format off
  enum class State : int8_t {
    CONNECT_REFUSED         = -5,  // Broker refused the connect with a code the standard leaves undefined.
    CONNECTION_TIMEOUT      = -4,  // Server did not answer within socketTimeout.
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

  /// @brief Constructs a PubSubClient with a TCP client.
  /// @param client Reference to the TCP client used for the connection.
  explicit PubSubClient(Client& client);

  /// @brief Constructs a PubSubClient with a server IP address.
  /// @param addr Server IP address.
  /// @param port Server port number.
  /// @param client Reference to the TCP client.
  PubSubClient(const IPAddress& addr, uint16_t port, Client& client);

  /// @brief Constructs a PubSubClient with a server IP byte array and callback.
  /// @param ip Pointer to a 4-byte array holding the server IP address.
  /// @param port Server port number.
  /// @param callback Callback invoked when a message is received.
  /// @param client Reference to the TCP client.
  PubSubClient(const uint8_t* ip, uint16_t port, MqttCallback callback, Client& client);

  /// @brief Constructs a PubSubClient with a server domain name and callback.
  /// @param domain Null-terminated server domain name string.
  /// @param port Server port number.
  /// @param callback Callback invoked when a message is received.
  /// @param client Reference to the TCP client.
  PubSubClient(const char* domain, uint16_t port, MqttCallback callback, Client& client);

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
  PubSubClient& setServer(const IPAddress& ip, uint16_t port);

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
  /// @details Runs inside loop(), with the message still in the packet buffer. Publishing from it
  /// is allowed: everything the acknowledgement of that message needs has been read out before it
  /// is called, so the buffer is the callback's to overwrite. Calling loop() from it is not.
  /// @param callback Function to call on message arrival.
  /// @return Reference to this instance for method chaining.
  PubSubClient& setCallback(MqttCallback callback);

  /// @brief Sets the MQTT keep-alive interval.
  /// @param keepAlive Keep-alive interval in seconds.
  /// @return Reference to this instance for method chaining.
  PubSubClient& setKeepAlive(uint16_t keepAlive);

  /// @brief Sets the socket read timeout.
  /// @param timeout Socket timeout in seconds.
  /// @return Reference to this instance for method chaining.
  PubSubClient& setSocketTimeout(uint16_t timeout);

  /// @brief Resizes the internal packet buffer.
  /// @param size New buffer size in bytes; must be between 1 and MQTT_MAX_PACKET_SIZE.
  /// @return `true` if the size is valid and was applied; otherwise `false`.
  [[nodiscard]] bool setBufferSize(uint16_t size);

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
  /// @param willQos QoS level the broker publishes the Last Will at (0, 1 or 2); more is refused.
  /// @param willRetain Whether the broker should retain the Last Will message.
  /// @param willMessage Null-terminated Last Will payload; may be `nullptr`.
  /// @return `true` if the connection was established; otherwise `false`.
  [[nodiscard]] bool connect(const char* id, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage);

  /// @brief Connects to the MQTT broker with credentials and a Last Will message.
  /// @param id Null-terminated MQTT client identifier.
  /// @param user Null-terminated username; may be `nullptr`.
  /// @param pass Null-terminated password; may be `nullptr`.
  /// @param willTopic Null-terminated Last Will topic.
  /// @param willQos QoS level the broker publishes the Last Will at (0, 1 or 2); more is refused.
  /// @param willRetain Whether the broker should retain the Last Will message.
  /// @param willMessage Null-terminated Last Will payload; may be `nullptr`.
  /// @return `true` if the connection was established; otherwise `false`.
  [[nodiscard]] bool connect(const char* id, const char* user, const char* pass, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage);

  /// @brief Connects to the MQTT broker with full options.
  /// @param id Null-terminated MQTT client identifier.
  /// @param user Null-terminated username; may be `nullptr`.
  /// @param pass Null-terminated password; may be `nullptr`.
  /// @param willTopic Null-terminated Last Will topic; may be `nullptr` to disable.
  /// @param willQos QoS level the broker publishes the Last Will at (0, 1 or 2); more is refused.
  /// @param willRetain Whether the broker should retain the Last Will message.
  /// @param willMessage Null-terminated Last Will payload; may be `nullptr`.
  /// @param cleanSession Whether to request a clean session from the broker.
  /// @return `true` if the connection was established; otherwise `false`.
  [[nodiscard]] bool connect(const char* id, const char* user, const char* pass, const char* willTopic, uint8_t willQos, bool willRetain, const char* willMessage, bool cleanSession);

  /// @brief Sends an MQTT DISCONNECT packet and closes the TCP connection.
  void disconnect();

  /// @brief Publishes a string payload to a topic.
  /// @param topic Null-terminated MQTT topic; `nullptr` is refused.
  /// @param payload Null-terminated payload string; may be `nullptr` for an empty payload.
  /// @param retained Whether the broker should retain the message (default: `false`).
  /// @return `true` if the message was sent successfully; otherwise `false`.
  [[nodiscard]] bool publish(const char* topic, const char* payload, bool retained = false);

  /// @brief Publishes a binary payload to a topic.
  /// @param topic Null-terminated MQTT topic; `nullptr` is refused.
  /// @param payload Pointer to the payload buffer.
  /// @param plength Payload length in bytes.
  /// @param retained Whether the broker should retain the message (default: `false`).
  /// @return `true` if the message was sent successfully; otherwise `false`.
  [[nodiscard]] bool publish(const char* topic, const uint8_t* payload, uint16_t plength, bool retained = false);

  /// @brief Publishes a PROGMEM string payload to a topic.
  /// @param topic Null-terminated MQTT topic; `nullptr` is refused.
  /// @param payload Null-terminated PROGMEM string; may be `nullptr` for an empty payload.
  /// @param retained Whether the broker should retain the message.
  /// @return `true` if the message was sent successfully; otherwise `false`.
  [[nodiscard]] bool publish_P(const char* topic, const char* payload, bool retained);

  /// @brief Publishes a binary PROGMEM payload to a topic.
  /// @param topic Null-terminated MQTT topic; `nullptr` is refused.
  /// @param payload Pointer to PROGMEM payload buffer.
  /// @param plength Payload length in bytes.
  /// @param retained Whether the broker should retain the message.
  /// @return `true` if the message was sent successfully; otherwise `false`.
  [[nodiscard]] bool publish_P(const char* topic, const uint8_t* payload, uint16_t plength, bool retained);

  /// @brief Subscribes to a topic and waits for the broker's answer.
  /// @details Blocks for up to the socket timeout, as the connect handshake does: a subscription
  /// the broker refuses leaves the client connected but deaf, which is worth knowing here rather
  /// than from the silence that follows.
  /// @param topic Null-terminated MQTT topic filter.
  /// @param qos QoS level (0 or 1; default: 0).
  /// @return `true` if the broker granted the subscription; otherwise `false`.
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
  [[nodiscard]] bool connected();

  /// @brief Returns the current MQTT connection state.
  /// @return The connection state as a State enum value.
  [[nodiscard]] State state() const;

  /// @brief How many keep-alive pings the TCP client refused to take since this object was built.
  /// @details A refusal is retried rather than dropped, so a non-zero count is not itself a fault:
  /// it says the client does refuse writes on this link, which is what tells a keep-alive drop
  /// caused here apart from one caused by the network. Saturates rather than wrapping.
  [[nodiscard]] uint16_t getRefusedPingCount() const;

private:
  /// @brief Fills the packet buffer with a CONNECT packet.
  /// @details Stops the client and returns 0 when a string would not fit the buffer.
  /// @param id Client id.
  /// @param user Username, or `nullptr` for none.
  /// @param pass Password, or `nullptr` for none.
  /// @param willTopic Last Will topic, or `nullptr` for none.
  /// @param willQos Last Will QoS.
  /// @param willRetain Whether the broker retains the Last Will message.
  /// @param willMessage Last Will payload, or `nullptr` for an empty one.
  /// @param cleanSession Whether the broker discards any previous session.
  /// @return Total buffer length reached, or 0 when a string did not fit.
  [[nodiscard]] uint16_t buildConnectPacket(const char* id, const char* user, const char* pass, const char* willTopic,
                                            uint8_t willQos, bool willRetain, const char* willMessage, bool cleanSession);

  /// @brief Waits for the CONNACK and records what it said.
  /// @details Tears the connection down on a timeout, a malformed answer or a refusal.
  /// @return `true` when the broker accepted the connection; otherwise, `false`.
  [[nodiscard]] bool awaitConnAck();

  /// @brief Waits for the SUBACK answering the packet id given, dispatching whatever precedes it.
  /// @details Drops the connection when nothing parseable arrives before the socket timeout: the
  /// bytes already taken off the socket cannot be put back for the session to carry on.
  /// @param packetId Packet id the SUBSCRIBE went out with.
  /// @return `true` when the broker granted the filter; `false` when it refused it or said nothing.
  [[nodiscard]] bool awaitSubAck(uint16_t packetId);

  /// @brief Whether the packet in `buffer` is the SUBACK for the packet id given.
  /// @param packetId Packet id the SUBSCRIBE went out with.
  /// @return `true` when it is that SUBACK; otherwise, `false`.
  [[nodiscard]] bool isSubAckFor(uint16_t packetId) const;

  /// @brief Sends a framed MQTT packet by prepending the fixed and variable-length header.
  /// @param header MQTT fixed-header byte.
  /// @param buf Buffer containing the payload, with MQTT_MAX_HEADER_SIZE bytes reserved at the start.
  /// @param length Payload length in bytes.
  /// @return `true` if all bytes were sent; otherwise `false`.
  bool write(uint8_t header, uint8_t* buf, uint16_t length);

  /// @brief Checks whether a string fits in the remaining buffer space.
  ///        Calls tcpClient->stop() if the string does not fit.
  /// @param length Bytes already used in the buffer.
  /// @param str Null-terminated string to check.
  /// @return `true` if the string fits; otherwise `false`.
  bool checkStringLength(uint16_t length, const char* str) const;

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

  /// @brief Hands the due keep-alive ping to the TCP client, retrying a refusal at intervals.
  /// @param t Current timestamp from millis().
  /// @return `false` once the ping has gone unsent for a whole keep-alive interval; `true` while
  ///         it is still worth asking.
  [[nodiscard]] bool keepAlivePing(uint32_t t);

  /// @brief How far the reader has got through the packet it is assembling.
  /// @details The reader keeps its place between `loop()` calls, so a packet that arrives in
  /// pieces is continued rather than waited for: nothing below blocks on the socket.
  enum class RxPhase : uint8_t {
    Idle = 0U,     // No packet in progress.
    Header = 1U,   // Collecting the fixed header byte and the remaining-length field.
    Payload = 2U,  // Collecting the bytes the remaining-length field announced.
  };

  /// @brief What one pass over the socket made of the packet in progress.
  enum class RxResult : uint8_t {
    Incomplete = 0U,  // Ran out of bytes; come back next pass.
    Complete = 1U,    // The whole packet is in.
    Malformed = 2U,   // The stream cannot be trusted, and cannot be brought back into step.
  };

  /// @brief Takes whatever the socket has ready and dispatches a packet once it is whole.
  /// @param t Current timestamp from millis(), recorded as inbound activity.
  /// @return `false` when the connection was given up, matching `loop()`'s own result.
  [[nodiscard]] bool pumpReader(uint32_t t);

  /// @brief Collects the fixed header and the remaining-length field.
  /// @return `Complete` once the length is known and the phase has moved on to the payload,
  ///         `Incomplete` while bytes of it are still missing, `Malformed` for a length field
  ///         that cannot be parsed or announces less than a PUBLISH needs.
  RxResult advanceHeader();

  /// @brief Collects the announced payload, buffering and streaming what belongs where.
  /// @return `Complete` once every announced byte has been taken off the socket.
  RxResult advancePayload();

  /// @brief Takes several payload bytes at once, keeping what fits and discarding the rest.
  /// @param take How many bytes to take; the caller has checked that many are ready.
  void takePayloadBulk(uint32_t take);

  /// @brief Starts a packet over, whatever became of the last one.
  void resetReader();

  /// @brief Runs the reader until a whole packet is in or the socket timeout runs out.
  /// @details Only the connect handshake uses this: until the CONNACK arrives there is nothing
  /// else for the caller to get on with, so waiting here costs nothing the main loop would miss.
  /// @return What became of the packet; the bytes are in `buffer`, `rxLen` of them.
  RxResult readPacketBlocking();

  /// @brief Dispatches a packet the reader has finished assembling.
  /// @details Reads it out of `buffer`, `rxLen` bytes with `rxLengthLength` of remaining-length
  /// field, and answers it: a PUBLISH reaches the callback (and is acknowledged at QoS 1), a
  /// PINGREQ is answered, a PINGRESP clears the outstanding ping.
  /// @param t Current timestamp from millis(), used to update lastOutActivity when it answers.
  void dispatchPacket(uint32_t t);

  Client& tcpClient;                              // The TCP client the session runs over; fixed for this object's life.
  uint8_t buffer[defaultBufferSize]{};            // Internal packet buffer, zero-initialised.
  // Scratch for the bytes of an oversized packet, which are read only to be thrown away.
  static constexpr uint8_t discardChunkSize = 64U;

  uint16_t bufferSize = defaultBufferSize;        // Active buffer size; may be reduced by setBufferSize().
  uint16_t keepAlive = defaultKeepAlive;          // Keep-alive interval in seconds.
  uint16_t socketTimeout = defaultSocketTimeout;  // Socket read timeout in seconds.
  uint16_t nextMsgId = 0U;                        // Next MQTT message ID (1–65535; 0 is reserved).
  uint32_t lastOutActivity = 0U;                  // Timestamp (ms) of the last outgoing packet.
  uint32_t lastInActivity = 0U;                   // Timestamp (ms) of the last incoming packet.
  RxPhase rxPhase = RxPhase::Idle;                // How far the packet being assembled has got.
  uint16_t rxLen = 0U;                            // Bytes of the packet stored in `buffer`.
  uint8_t rxLengthLength = 0U;                    // Bytes the remaining-length field took.
  uint32_t rxMultiplier = 1U;                     // Place value of the next remaining-length digit.
  uint32_t rxRemaining = 0U;                      // Bytes the remaining-length field announced.
  uint32_t rxPayloadDone = 0U;                    // Announced bytes taken off the socket so far.
  bool rxOversized = false;                       // Packet longer than the buffer: taken off the socket, then dropped.
  uint32_t rxStartedMs = 0U;                      // millis() when the first byte of the packet arrived.
  bool pingOutstanding = false;                   // `true` if a PINGREQ was sent without a PINGRESP.
  bool pingUnsent = false;                        // `true` while a due PINGREQ has not been taken by the client.
  uint32_t pingUnsentSince = 0U;                  // Timestamp (ms) of the first refusal of the pending PINGREQ.
  uint32_t lastPingAttempt = 0U;                  // Timestamp (ms) of the last attempt to hand the PINGREQ over.
  uint16_t refusedPings = 0U;                     // Keep-alive pings the client would not take; saturates at its maximum.
  MqttCallback callback = nullptr;                // User callback invoked on message receipt.
  IPAddress ip;                                   // Server IP address (used when domain is nullptr).
  const char* domain = nullptr;                   // Server domain name; takes priority over ip when set.
  uint16_t port = 0U;                             // Server port number.
  State connectionState = State::DISCONNECTED;    // Current MQTT connection state.
};
