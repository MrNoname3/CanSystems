#pragma once
// Native-test shim for connectivity.hpp: a minimal Connectivity + MqttBase test double so MQTT
// handler libraries (e.g. mqttCommon) can be unit-tested on the host. The real connectivity lib is
// lib_ignored for native_test, so this header stands in. MqttBase records sendResponse / sendMessage
// / shutdownMqtt calls for assertions; HADiscovery is the real (host-compatible) implementation.
//
// The method signatures must match the real MqttBase — the ESP builds compile the handlers against
// the real class, so any divergence here surfaces as a native-only compile error.
#include <stdint.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>
#include "taskHandler.hpp"
#include "haDiscovery.hpp"
#include <ArduinoJson.h>

class MqttBase;

class Connectivity {
public:
  using HADiscovery = ::HADiscovery;
};

class MqttBase : public virtual Task {
public:
  enum class Response : uint8_t { NACK = 0U, ACK = 1U };

  [[nodiscard]] bool init() override = 0;
  [[nodiscard]] bool run() override = 0;
  virtual void messageArrivedCallback(JsonDocument& payloadJson) = 0;
  virtual bool publishDiscovery() { return true; }

  [[nodiscard]] static constexpr uint8_t getSubtopicSize() { return 16U; }

  // These mirror the real MqttBase (instance methods that reach into Connectivity); the shim records
  // into static fields instead, so clang-tidy would make them static — kept non-static to match.
  [[nodiscard]] bool sendMessage(const char* payload) {                     // NOLINT(readability-convert-member-functions-to-static)
    if(payload == nullptr) { return false; }
    lastMessage = payload; ++messageCount; return sendResult;
  }
  [[nodiscard]] bool sendResponse(Response response, uint16_t command = 0U, uint32_t errCode = 0U) {  // NOLINT(readability-convert-member-functions-to-static)
    (void)command; lastResponse = response; lastErrCode = errCode; ++responseCount; return sendResult;
  }
  [[nodiscard]] bool doPublishEntityDiscovery(const HADiscovery::EntityConfig& config) {  // NOLINT(readability-convert-member-functions-to-static)
    (void)config; return true;
  }
  [[nodiscard]] bool doPublishCanDeviceEntityDiscovery(const char* subtopic,              // NOLINT(readability-convert-member-functions-to-static)
                                                       const HADiscovery::EntityConfig& config,
                                                       const HADiscovery::CanDeviceConfig& canDevConfig) {
    (void)config; (void)canDevConfig;
    canDiscoverySubtopics.emplace_back(subtopic);
    return true;
  }
  [[nodiscard]] bool sendRetainedSubtopic(const char* subSubTopic, const char* payload) { // NOLINT(readability-convert-member-functions-to-static)
    retainedMessages.emplace_back(subSubTopic, payload); return sendResult;
  }
  [[nodiscard]] bool sendSubtopicMessage(const char* subSubTopic, const char* payload) {  // NOLINT(readability-convert-member-functions-to-static)
    subtopicMessages.emplace_back(subSubTopic, payload); return sendResult;
  }
  [[nodiscard]] const char* getSenderTopicStr() const { return senderTopicStr; }          // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] const char* getClientNameStr() const { return clientNameStr; }            // NOLINT(readability-convert-member-functions-to-static)
  void shutdownMqtt() { ++shutdownCount; }                                  // NOLINT(readability-convert-member-functions-to-static)
  [[nodiscard]] const char* getSubtopic() const { return subtopic; }

  // ---- test inspection (static so tests can read them without a handle) ----
  static inline Response lastResponse  = Response::NACK;
  static inline uint32_t lastErrCode   = 0U;
  static inline int      responseCount = 0;
  static inline int      messageCount  = 0;
  static inline int      shutdownCount = 0;
  static inline bool     sendResult    = true;
  static inline std::string lastMessage;                                        // Last sendMessage payload.
  static inline std::vector<std::pair<std::string, std::string>> retainedMessages;   // (subSubTopic, payload) pairs.
  static inline std::vector<std::pair<std::string, std::string>> subtopicMessages;   // (subSubTopic, payload) pairs.
  static inline std::vector<std::string> canDiscoverySubtopics;                 // CAN device discovery entity subtopics.
  // Fixed connection identity matching the real Connectivity's formats ("iot/dtos/<mac>/" etc.).
  static constexpr const char senderTopicStr[] = "iot/dtos/aabbccddeeff/";
  static constexpr const char clientNameStr[]  = "esp32_can_aabbccddeeff";
  static void resetState() {
    lastResponse = Response::NACK; lastErrCode = 0U;
    responseCount = 0; messageCount = 0; shutdownCount = 0; sendResult = true;
    lastMessage.clear(); retainedMessages.clear(); subtopicMessages.clear(); canDiscoverySubtopics.clear();
  }

  MqttBase(const MqttBase&) = delete;
  MqttBase& operator=(const MqttBase&) = delete;
  MqttBase(MqttBase&&) = delete;
  MqttBase& operator=(MqttBase&&) = delete;

protected:
  MqttBase(Connectivity& connectivity, const char* subTopic) {
    (void)connectivity;
    if(subTopic != nullptr) {
      strncpy(subtopic, subTopic, sizeof(subtopic) - 1U);
      subtopic[sizeof(subtopic) - 1U] = '\0';
    }
  }
  ~MqttBase() override = default;

private:
  char subtopic[16] = {0};
};
