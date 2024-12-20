#include "configHandler.hpp"
#include <LittleFS.h>                                               /// Use FLASH filesystem.
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "common.hpp"                                               /// Common definitions and functions.

uint8_t ConfigHandler::error = 0U;

void ConfigHandler::setError(ERROR err) {
  if(err != ERROR::NONE) {
    error |= static_cast<uint8_t>(err);
  }
}

uint8_t ConfigHandler::getError() {
  return error;
}

void ConfigHandler::clearError() {
  error = 0U;
}

uint16_t ConfigHandler::getWifiConfig(char (&ssid)[maxWifiSsidSize], char (&password)[maxWifiPasswordSize]) {
  clearError();
  const bool wifiFileExists = LittleFS.exists(FPSTR(FileName::getWifiConfigLocation()));
  if(!wifiFileExists) {
    setError(ERROR::NO_CONFIG_FILE);
    return getError();
  }

  File wifiFile = LittleFS.open(FPSTR(FileName::getWifiConfigLocation()), "r");
  if(!wifiFile) {
    setError(ERROR::CANNOT_OPEN_FILE);
    wifiFile.close();
    return getError();
  }

  JsonDocument wifiJson;
  DeserializationError deserializationError = deserializeJson(wifiJson, wifiFile);
  const bool isDeserializationSuccessful  = (deserializationError == DeserializationError::Code::Ok);
  if(isDeserializationSuccessful ) {
    JsonVariant ssidJsonVar = wifiJson[F("ssid")];
    JsonVariant passwordJsonVar = wifiJson[F("password")];
    const bool ssidKeyOk = ssidJsonVar.is<const char*>();
    const bool passwordKeyOk = passwordJsonVar.is<const char*>();
    if(!ssidKeyOk) { setError(ERROR::MISSING_SSID_KEY); }
    if(!passwordKeyOk) { setError(ERROR::MISSING_PWD_KEY); }
    if(ssidKeyOk && passwordKeyOk) {
      const char* ssidJsonPtr = ssidJsonVar.as<const char*>();
      const char* passJsonPtr = passwordJsonVar.as<const char*>();
      const uint8_t ssidLength = strnlen(ssidJsonPtr, maxWifiSsidSize);
      const uint8_t passLength = strnlen(passJsonPtr, maxWifiPasswordSize);
      const bool ssidLengthValid = (ssidLength > 0U) && (ssidLength < maxWifiSsidSize);
      const bool passLengthValid = (passLength > 0U) && (passLength < maxWifiPasswordSize);
      if(!ssidLengthValid) { setError(ERROR::SSID_LENGTH_ERR); }
      if(!passLengthValid) { setError(ERROR::PWD_LENGTH_ERR); }
      if(ssidLengthValid && passLengthValid) {
        strlcpy(ssid, ssidJsonPtr, maxWifiSsidSize);
        strlcpy(password, passJsonPtr, maxWifiPasswordSize);
      }
    }
  } else {
    setError(ERROR::JSON_PARSING_ERROR);
  }

  wifiFile.close();
  return getError();
}