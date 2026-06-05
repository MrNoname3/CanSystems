#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "common.hpp"                                               /// Common definitions and functions.
#include "mqttUploader.hpp"                                         /// Upload handler the captured frames are queued into.
#include "freertos/FreeRTOS.h"                                      /// FreeRTOS base (task handle).
#include "freertos/task.h"                                          /// Task creation / vTaskDelay.

/// @brief Periodically captures a JPEG frame from an ESP32-CAM and queues it for upload.
/// @details Runs the capture in its own FreeRTOS task: capturing blocks for tens of milliseconds
/// (sensor exposure + JPEG + DMA), which would otherwise stall the cooperative loop. The task
/// sleeps between captures with vTaskDelay (no CPU spent while idle). The frame is captured straight
/// into the camera's PSRAM frame buffer (no SD card); the borrowed buffer is handed to MqttUploader
/// and returned to the driver only once the upload completes. Falls back to a smaller DRAM frame if
/// no PSRAM. The producer side only touches the (mutex-protected) upload queue, never the MQTT client.
class CameraHandler final {
private:
  static constexpr uint32_t xclkFreqHz       = 20000000U;           // Camera master clock frequency.
  static constexpr uint8_t  uploadNameSize   = 28U;                 // Buffer size for the upload name ("IMG_<utc-stamp>.jpg").
  static constexpr uint8_t  cameraInitAttempts     = 3U;            // Tries for the sensor SCCB probe (transient init failures).
  static constexpr uint16_t cameraInitRetryDelayMs = 150U;          // Delay between camera init attempts.

  // Capture task configuration.
  static constexpr uint32_t taskStackSize    = 4096U;               // Capture task stack size in bytes (ESP-IDF xTaskCreate uses bytes).
  static constexpr uint8_t  taskPriority     = 1U;                  // Same as the Arduino loop task.
  static constexpr uint8_t  taskCore         = 1U;                  // APP_CPU (the Arduino loop core); workers mostly sleep.

  // Defaults applied when /config/cam.json is missing or a key is absent.
  static constexpr uint8_t  defaultFrameSize   = 9U;                // esp_camera framesize_t index (9 = FRAMESIZE_SVGA).
  static constexpr uint8_t  defaultJpegQuality = 12U;               // JPEG quality (lower = better quality, larger file).
  static constexpr uint8_t  defaultFbCount     = 2U;                // Frame buffers (double-buffered when PSRAM is available).
  static constexpr uint8_t  fallbackFrameSize  = 5U;                // esp_camera framesize_t index (5 = FRAMESIZE_QVGA) when no PSRAM.
  static constexpr bool     defaultFlashEnabled    = false;         // Fire the on-board flash LED for each capture by default.
  static constexpr uint8_t  defaultFlashBrightness = 128U;          // Default flash PWM duty (0..255), config-overridable.

  // Flash LED: AI-Thinker on-board high-power white LED on GPIO 4, driven by LEDC PWM on its own channel.
  static constexpr uint8_t  flashLedcChannel    = 4U;               // Avoids the camera XCLK channel (LEDC_CHANNEL_0 / timer 0).
  static constexpr uint32_t flashLedcFreqHz     = 5000U;            // PWM frequency.
  static constexpr uint8_t  flashLedcResolution = 8U;               // 8-bit duty -> brightness range 0..255.
  static constexpr uint32_t flashSettleMs       = 150U;             // Let exposure (AEC/AGC) adapt before grabbing the lit frame.

public:
  /// @brief Constructs a CameraHandler.
  /// @param uploader Upload handler that captured frames are queued into.
  /// @param captureIntervalMs Default interval between captures (overridable via /config/cam.json).
  CameraHandler(MqttUploader& uploader, uint32_t captureIntervalMs);

  /// @brief Default destructor.
  ~CameraHandler() = default;

  /// @brief Initializes the camera driver and spawns the capture task.
  /// @return `true` always; a camera init failure is logged and the task is not spawned so the rest
  ///         of the device (networking, OTA) still boots.
  bool begin();

  CameraHandler(const CameraHandler&) = delete;                     // Define copy constructor.
  CameraHandler& operator=(const CameraHandler&) = delete;          // Define copy assignment operator.
  CameraHandler(CameraHandler&&) = delete;                          // Define move constructor.
  CameraHandler& operator=(CameraHandler&&) = delete;               // Define move assignment operator.

private:
  /// @brief Loads capture parameters from /config/cam.json, keeping current values as defaults.
  void loadConfig();

  /// @brief FreeRTOS task entry: capture -> enqueue -> vTaskDelay(interval), forever.
  /// @param arg The owning CameraHandler instance.
  static void captureTask(void* arg);

  /// @brief Captures a single frame and queues it for upload.
  void captureAndQueue();

  /// @brief Release callback handed to the uploader: returns the borrowed frame buffer to the driver.
  /// @param ctx The `camera_fb_t*` to return.
  static void releaseFrame(void* ctx);

  MqttUploader& uploader;                                           // Upload handler frames are queued into.
  uint32_t captureIntervalMs;                                       // Interval between captures (config-overridable).
  uint32_t frameSequence;                                           // Monotonic counter used in upload names.
  uint8_t frameSize;                                                // esp_camera framesize_t index (config-overridable).
  uint8_t jpegQuality;                                              // JPEG quality (config-overridable).
  uint8_t fbCount;                                                  // Number of frame buffers (config-overridable).
  bool flashEnabled;                                                // Whether to fire the flash for each capture (config-overridable).
  uint8_t flashBrightness;                                          // Flash PWM duty 0..255 when enabled (config-overridable).
  bool cameraReady;                                                 // Whether the camera initialized successfully.
  TaskHandle_t taskHandle;                                          // Handle of the spawned capture task.
};
