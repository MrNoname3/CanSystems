#include "mqttUploader.hpp"
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "common.hpp"                                               /// Common definitions and functions.

MqttUploader::MqttUploader(Connectivity& connectivity, const char* subtopic) :
  MqttBase(connectivity, subtopic),
  dataUploader(uploadCompleteCb)
{}

bool MqttUploader::init() { // NOLINT(readability-convert-member-functions-to-static)
  return true;
}

bool MqttUploader::run() {
  if(isUploadDone) {
    isUploadDone = false;
    if(!isUploadOk) {
      const uint32_t errCode = dataUploader.getErrorCode();
      Logger::get().printf_P(PSTR("[UP] Upload failed!\r\n  Code: %u\r\n"), errCode);
    }
  }
  // Publish the next payload, if the uploader produced one.
  char payload[payloadBufferSize] = {'\0'};
  const size_t payloadLen = dataUploader.prepareMessage(payload, sizeof(payload));
  if(payloadLen > 0U) {
    if(!sendMessage(payload)) {
      Logger::get().printf_P(PSTR("[UP] Failed to publish upload payload!\r\n"));
      // Treat a publish failure as a NACK so the state machine aborts the job cleanly.
      dataUploader.notifyAck(false);
    }
  }
  dataUploader.run();
  return true;
}

void MqttUploader::messageArrivedCallback(JsonDocument& payloadJson) {
  // Server response mirrors MqttBase::sendResponse: {"type":<NACK|ACK>, ...}.
  JsonVariant typeJsonVar = payloadJson[F("type")];
  if(!typeJsonVar.is<uint8_t>()) {
    Logger::get().printf_P(PSTR("[UP] Unknown upload response!\r\n"));
    return;
  }
  const bool ack = (typeJsonVar.as<uint8_t>() == static_cast<uint8_t>(MqttBase::Response::ACK));
  dataUploader.notifyAck(ack);
}

void MqttUploader::uploadCompleteCb(bool ok) {
  isUploadDone = true;
  isUploadOk = ok;
}
