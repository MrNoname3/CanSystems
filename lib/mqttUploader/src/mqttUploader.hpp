#pragma once
// ESP-only (Connectivity/PubSubClient). Guarded so non-ESP builds / native static analysis skip it.
#if defined(ESP8266) || defined(ESP32)
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "dataUploader.hpp"                                         /// Client -> server file upload engine.

/// @brief MQTT handler that streams queued uploads to the server.
/// @details The send-path counterpart of `MqttCommon`: where `MqttCommon` owns a `DataTransfer`
/// to receive files, this owns a `DataUploader` to send them. It is the transport binding between
/// the transport-agnostic `DataUploader` and `Connectivity`:
///   - `run()` pulls the next JSON payload from the uploader and publishes it on the "upload" subtopic.
///   - `messageArrivedCallback()` forwards the server's ACK/NACK back into the uploader.
/// Producers (e.g. `CameraHandler`) enqueue work through `enqueue()` / `enqueueFile()`.
class MqttUploader final : public MqttBase {
private:
  static constexpr uint16_t payloadBufferSize = 512U;               // Buffer for an outgoing upload JSON payload.

public:
  /// @brief Constructs a new MqttUploader.
  /// @param connectivity Reference to the Connectivity object managing MQTT.
  /// @param subtopic Subtopic used for upload messages (e.g. "upload").
  MqttUploader(Connectivity& connectivity, const char* subtopic);

  /// @brief Destructor of the object.
  ~MqttUploader() override = default;

  /// @brief Initializes the handler.
  /// @return `true` on success.
  bool init() override;

  /// @brief Pumps the upload state machine: publishes the next payload and processes timeouts.
  /// @return `true` on success.
  bool run() override;

  /// @brief Processes an incoming server response (ACK/NACK) in JSON format.
  /// @param payloadJson JSON document containing the received MQTT message.
  void messageArrivedCallback(JsonDocument& payloadJson) override;

  /// @brief Queues an upload from a borrowed (PS)RAM buffer. See `DataUploader::enqueue`.
  [[nodiscard]] bool enqueue(const char* name, const uint8_t* data, uint32_t size,
                             DataUploader::ReleaseCb release = nullptr, void* ctx = nullptr) {
    return dataUploader.enqueue(name, data, size, release, ctx);
  }

  /// @brief Queues an upload sourced from a LittleFS file. See `DataUploader::enqueueFile`.
  [[nodiscard]] bool enqueueFile(const char* name, const char* path) {
    return dataUploader.enqueueFile(name, path);
  }

  /// @brief Whether the uploader can accept another job.
  [[nodiscard]] bool hasFreeSlot() const { return dataUploader.hasFreeSlot(); }

  MqttUploader(const MqttUploader&) = delete;                       // Define copy constructor.
  MqttUploader& operator=(const MqttUploader&) = delete;            // Define copy assignment operator.
  MqttUploader(MqttUploader&&) = delete;                            // Define move constructor.
  MqttUploader& operator=(MqttUploader&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Callback invoked by the uploader when a job completes.
  /// @param ok `true` on success, `false` on failure.
  static void uploadCompleteCb(bool ok);

  static inline bool isUploadDone = false;          // Flag set when a job has completed.
  static inline bool isUploadOk = false;            // Result of the completed job.

  DataUploader dataUploader;                         // Upload engine driven by this handler.
};

#endif  // defined(ESP8266) || defined(ESP32)
