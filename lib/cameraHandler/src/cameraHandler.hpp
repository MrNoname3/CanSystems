#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "taskHandler.hpp"                                          /// Task base class for periodic execution.
#include "common.hpp"                                               /// Common definitions and functions.
#include "mqttUploader.hpp"                                         /// Upload handler the captured frames are queued into.

/// @brief Periodically captures a JPEG frame from an ESP32-CAM and queues it for upload.
/// @details The frame is captured straight into the camera's PSRAM frame buffer (no SD card); the
/// borrowed buffer is handed to `MqttUploader` and returned to the driver only once the upload
/// completes. If PSRAM is unavailable the camera driver falls back to a smaller, DRAM-backed frame.
///
/// This is a skeleton: pin map and capture parameters target the AI-Thinker ESP32-CAM and will be
/// refined later (configurable resolution/quality, optional flash-backed buffering, RTC-time names).
class CameraHandler final : public Task {
private:
  static constexpr uint32_t xclkFreqHz       = 20000000U;           // Camera master clock frequency.
  static constexpr uint8_t  jpegQuality      = 12U;                 // JPEG quality (lower = better quality, larger file).
  static constexpr uint8_t  uploadNameSize   = 24U;                 // Buffer size for the generated upload name.

public:
  /// @brief Constructs a CameraHandler.
  /// @param uploader Upload handler that captured frames are queued into.
  /// @param captureIntervalMs Interval between captures, in milliseconds.
  CameraHandler(MqttUploader& uploader, uint32_t captureIntervalMs);

  /// @brief Default destructor.
  ~CameraHandler() override = default;

  /// @brief Initializes the camera driver.
  /// @return `true` always; a camera init failure is logged and captures are skipped so the rest
  ///         of the device (networking, OTA) still boots.
  bool init() override;

  /// @brief Captures and queues a frame when the interval elapses and the uploader has a free slot.
  /// @return `true` on success.
  bool run() override;

  CameraHandler(const CameraHandler&) = delete;                     // Define copy constructor.
  CameraHandler& operator=(const CameraHandler&) = delete;          // Define copy assignment operator.
  CameraHandler(CameraHandler&&) = delete;                          // Define move constructor.
  CameraHandler& operator=(CameraHandler&&) = delete;               // Define move assignment operator.

private:
  /// @brief Captures a single frame and queues it for upload.
  void captureAndQueue();

  /// @brief Release callback handed to the uploader: returns the borrowed frame buffer to the driver.
  /// @param ctx The `camera_fb_t*` to return.
  static void releaseFrame(void* ctx);

  MqttUploader& uploader;                                           // Upload handler frames are queued into.
  uint32_t captureIntervalMs;                                       // Interval between captures.
  uint32_t captureTimer;                                            // Timestamp of the last capture attempt.
  uint32_t frameSequence;                                           // Monotonic counter used in upload names.
  bool cameraReady;                                                 // Whether the camera initialized successfully.
};
