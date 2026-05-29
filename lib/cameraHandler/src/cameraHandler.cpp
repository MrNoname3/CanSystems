#include "cameraHandler.hpp"
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
  captureTimer(0U),
  frameSequence(0U),
  cameraReady(false)
{}

bool CameraHandler::init() {
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

  // Prefer PSRAM for a larger frame and double buffering; fall back to a smaller DRAM frame.
  if(psramFound()) {
    config.frame_size  = FRAMESIZE_SVGA;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count    = 2U;
  } else {
    config.frame_size  = FRAMESIZE_QVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count    = 1U;
  }

  const esp_err_t err = esp_camera_init(&config);
  cameraReady = (err == ESP_OK);
  Logger::get().printf_P(PSTR("[CAM] Camera init: %s\r\n"), Str::getStateStr(cameraReady));
  if(!cameraReady) {
    Logger::get().printf_P(PSTR("  esp_camera_init error: 0x%x\r\n"), err);
  }
#else
  Logger::get().printf_P(PSTR("[CAM] Camera unsupported on this platform.\r\n"));
#endif
  captureTimer = millis();
  return true;  // Never block device boot on a camera failure.
}

bool CameraHandler::run() {
  if(!cameraReady) { return true; }
  if(!Time::hasElapsed(millis(), captureTimer, captureIntervalMs)) { return true; }
  captureTimer = millis();
  if(!uploader.hasFreeSlot()) {
    Logger::get().printf_P(PSTR("[CAM] Upload queue full; skipping capture.\r\n"));
    return true;
  }
  captureAndQueue();
  return true;
}

void CameraHandler::captureAndQueue() {
#if defined(ESP32)
  camera_fb_t* fb = esp_camera_fb_get();
  if(fb == nullptr) {
    Logger::get().printf_P(PSTR("[CAM] Frame capture failed!\r\n"));
    return;
  }
  char name[uploadNameSize] = {'\0'};
  (void)snprintf_P(name, sizeof(name), PSTR("%08x.jpg"), frameSequence++);
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
