#include "cameraHandler.hpp"
#include "configHandler.hpp"                                        /// Loads the optional camera configuration file.
#include <ArduinoJson.h>                                            /// JSON document for the camera config.

namespace {
constexpr char PROGMEM camConfigPath[] = "/config/cam.json";       // Optional camera configuration file.
}  // namespace

#if defined(ESP32)
#include "esp_camera.h"                                             /// ESP32 camera driver (bundled with arduino-esp32).

namespace {
// AI-Thinker ESP32-CAM pin map.
constexpr int8_t PIN_PWDN  = 32;
constexpr int8_t PIN_RESET = -1;
constexpr int8_t PIN_XCLK  = 0;
constexpr int8_t PIN_SIOD  = 26;
constexpr int8_t PIN_SIOC  = 27;
constexpr int8_t PIN_Y9    = 35;
constexpr int8_t PIN_Y8    = 34;
constexpr int8_t PIN_Y7    = 39;
constexpr int8_t PIN_Y6    = 36;
constexpr int8_t PIN_Y5    = 21;
constexpr int8_t PIN_Y4    = 19;
constexpr int8_t PIN_Y3    = 18;
constexpr int8_t PIN_Y2    = 5;
constexpr int8_t PIN_VSYNC = 25;
constexpr int8_t PIN_HREF  = 23;
constexpr int8_t PIN_PCLK  = 22;
}  // namespace
#endif

CameraHandler::CameraHandler(MqttUploader& uploader, uint32_t captureIntervalMs) :
  uploader(uploader),
  captureIntervalMs(captureIntervalMs),
  frameSequence(0U),
  frameSize(defaultFrameSize),
  jpegQuality(defaultJpegQuality),
  fbCount(defaultFbCount),
  cameraReady(false),
  taskHandle(nullptr)
{}

void CameraHandler::loadConfig() {
  JsonDocument doc;
  if(ConfigHandler::loadJsonFile(camConfigPath, doc) != ConfigHandler::JsonLoadResult::Ok) {
    Logger::get().printf_P(PSTR("[CAM] No camera config; using defaults.\r\n"));
    return;
  }
  const uint32_t intervalSec = doc[F("intervalSec")] | (captureIntervalMs / 1000U);
  if(intervalSec > 0U) { captureIntervalMs = Time::secToMs(static_cast<uint16_t>(intervalSec)); }
  frameSize   = doc[F("framesize")]   | frameSize;
  jpegQuality = doc[F("jpegQuality")] | jpegQuality;
  fbCount     = doc[F("fbCount")]     | fbCount;
  Logger::get().printf_P(PSTR("[CAM] Config loaded: interval=%us framesize=%hhu quality=%hhu fb=%hhu\r\n"),
                         (captureIntervalMs / 1000U), frameSize, jpegQuality, fbCount);
}

bool CameraHandler::begin() {
  loadConfig();
#if defined(ESP32)
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = PIN_Y2;
  config.pin_d1       = PIN_Y3;
  config.pin_d2       = PIN_Y4;
  config.pin_d3       = PIN_Y5;
  config.pin_d4       = PIN_Y6;
  config.pin_d5       = PIN_Y7;
  config.pin_d6       = PIN_Y8;
  config.pin_d7       = PIN_Y9;
  config.pin_xclk     = PIN_XCLK;
  config.pin_pclk     = PIN_PCLK;
  config.pin_vsync    = PIN_VSYNC;
  config.pin_href     = PIN_HREF;
  config.pin_sccb_sda = PIN_SIOD;
  config.pin_sccb_scl = PIN_SIOC;
  config.pin_pwdn     = PIN_PWDN;
  config.pin_reset    = PIN_RESET;
  config.xclk_freq_hz = xclkFreqHz;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = jpegQuality;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  // Prefer PSRAM for the configured frame and buffering; fall back to a small DRAM frame otherwise.
  if(psramFound()) {
    config.frame_size  = static_cast<framesize_t>(frameSize);
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count    = fbCount;
  } else {
    config.frame_size  = static_cast<framesize_t>(fallbackFrameSize);
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count    = 1U;
    Logger::get().printf_P(PSTR("[CAM] No PSRAM; forcing QVGA single-buffer.\r\n"));
  }

  const esp_err_t err = esp_camera_init(&config);
  cameraReady = (err == ESP_OK);
  Logger::get().printf_P(PSTR("[CAM] Camera init: %s\r\n"), Str::getStateStr(cameraReady));
  if(!cameraReady) {
    Logger::get().printf_P(PSTR("  esp_camera_init error: 0x%x\r\n"), err);
  } else {
    // Spawn the capture task: it owns all blocking camera I/O and only touches the upload queue.
    const BaseType_t created = xTaskCreatePinnedToCore(
      captureTask, "camCapture", taskStackSize, this, taskPriority, &taskHandle, taskCore);
    if(created != pdPASS) {
      Logger::get().printf_P(PSTR("[CAM] Capture task creation failed!\r\n"));
      cameraReady = false;
    }
  }
#else
  Logger::get().printf_P(PSTR("[CAM] Camera unsupported on this platform.\r\n"));
#endif
  return true;  // Never block device boot on a camera failure.
}

void CameraHandler::captureTask(void* arg) {
  CameraHandler* const self = static_cast<CameraHandler*>(arg);
  for(;;) {
    if(self->uploader.hasFreeSlot()) {
      self->captureAndQueue();
    } else {
      Logger::get().printf_P(PSTR("[CAM] Upload queue full; skipping capture.\r\n"));
    }
    vTaskDelay(pdMS_TO_TICKS(self->captureIntervalMs));  // Sleep until the next capture (no CPU used).
  }
}

void CameraHandler::captureAndQueue() {
#if defined(ESP32)
  camera_fb_t* fb = esp_camera_fb_get();
  if(fb == nullptr) {
    Logger::get().printf_P(PSTR("[CAM] Frame capture failed!\r\n"));
    return;
  }
  // Phone-style UTC filename (e.g. IMG_20260522_121115Z.jpg). The clock is NTP-synced during
  // connectivity init, so it is valid here; fall back to the boot-relative counter just in case.
  char name[uploadNameSize] = {'\0'};
  char stamp[Time::utcFileStampSize] = {'\0'};
  if(Time::getUtcFileStamp(stamp, sizeof(stamp))) {
    (void)snprintf_P(name, sizeof(name), PSTR("IMG_%s.jpg"), stamp);
  } else {
    (void)snprintf_P(name, sizeof(name), PSTR("%08x.jpg"), frameSequence);
  }
  frameSequence++;
  // Hand the PSRAM frame buffer to the uploader; it is returned via releaseFrame() when done.
  const bool queued = uploader.enqueue(name, fb->buf, fb->len, releaseFrame, fb);
  if(!queued) {
    Logger::get().printf_P(PSTR("[CAM] Failed to queue frame; dropping.\r\n"));
    esp_camera_fb_return(fb);
    return;
  }
  Logger::get().printf_P(PSTR("[CAM] Frame queued: %s (%u bytes)\r\n"), name, fb->len);
#endif
}

void CameraHandler::releaseFrame(void* ctx) {
#if defined(ESP32)
  if(ctx != nullptr) {
    esp_camera_fb_return(static_cast<camera_fb_t*>(ctx));
  }
#else
  (void)ctx;
#endif
}
