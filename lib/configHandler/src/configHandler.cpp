#include "configHandler.hpp"
#include <LittleFS.h>                                               /// Use FLASH filesystem.
#include <ArduinoJson.h>                                            /// Handle JSON files.

ErrorState<ConfigHandler::WifiConfigError, uint8_t> ConfigHandler::wifiConfErrState;
ErrorState<ConfigHandler::ServerCertError, uint8_t> ConfigHandler::serverCertErrState;

uint8_t ConfigHandler::getWifiConfig(char (&ssid)[maxWifiSsidSize], char (&password)[maxWifiPasswordSize]) {
  wifiConfErrState.clearAllErrors();
  const bool wifiFileExists = LittleFS.exists(FPSTR(FileName::getWifiConfigLocation()));
  if(!wifiFileExists) {
    wifiConfErrState.setError(WifiConfigError::NO_WIFI_CONFIG_FILE);
    return wifiConfErrState.getRawErrorState();
  }

  File wifiFile = LittleFS.open(FPSTR(FileName::getWifiConfigLocation()), "r");
  if(!wifiFile) {
    wifiConfErrState.setError(WifiConfigError::CANNOT_OPEN_FILE);
    wifiFile.close();
    return wifiConfErrState.getRawErrorState();
  }

  JsonDocument wifiJson;
  DeserializationError deserializationError = deserializeJson(wifiJson, wifiFile);
  const bool isDeserializationSuccessful  = (deserializationError == DeserializationError::Code::Ok);
  if(isDeserializationSuccessful ) {
    JsonVariant ssidJsonVar = wifiJson[F("ssid")];
    JsonVariant passwordJsonVar = wifiJson[F("password")];
    const bool ssidKeyOk = ssidJsonVar.is<const char*>();
    const bool passwordKeyOk = passwordJsonVar.is<const char*>();
    if(!ssidKeyOk) { wifiConfErrState.setError(WifiConfigError::MISSING_SSID_KEY); }
    if(!passwordKeyOk) { wifiConfErrState.setError(WifiConfigError::MISSING_PWD_KEY); }
    if(ssidKeyOk && passwordKeyOk) {
      const char* ssidJsonPtr = ssidJsonVar.as<const char*>();
      const char* passJsonPtr = passwordJsonVar.as<const char*>();
      const uint8_t ssidLength = strnlen(ssidJsonPtr, maxWifiSsidSize);
      const uint8_t passLength = strnlen(passJsonPtr, maxWifiPasswordSize);
      const bool ssidLengthValid = (ssidLength > 0U) && (ssidLength < maxWifiSsidSize);
      const bool passLengthValid = (passLength > 0U) && (passLength < maxWifiPasswordSize);
      if(!ssidLengthValid) { wifiConfErrState.setError(WifiConfigError::SSID_LENGTH_ERR); }
      if(!passLengthValid) { wifiConfErrState.setError(WifiConfigError::PWD_LENGTH_ERR); }
      if(ssidLengthValid && passLengthValid) {
        strlcpy(ssid, ssidJsonPtr, maxWifiSsidSize);
        strlcpy(password, passJsonPtr, maxWifiPasswordSize);
      }
    }
  } else {
    wifiConfErrState.setError(WifiConfigError::JSON_PARSING_ERROR);
  }

  wifiFile.close();
  return wifiConfErrState.getRawErrorState();
}

uint8_t ConfigHandler::getServerCert(std::function<bool(Stream&, size_t)> storeCert) {
  serverCertErrState.clearAllErrors();
  const bool certFileExists = LittleFS.exists(FPSTR(FileName::getMqttServerCertLocation()));
  if(!certFileExists) {
    serverCertErrState.setError(ServerCertError::NO_SERVER_CERT_FILE);
    return serverCertErrState.getRawErrorState();
  }

  File certFile = LittleFS.open(FPSTR(FileName::getMqttServerCertLocation()), "r");
  if(!certFile) {
    serverCertErrState.setError(ServerCertError::CANNOT_OPEN_FILE);
    certFile.close();
    return serverCertErrState.getRawErrorState();
  }

  if(certFile.size() > 0U) {
    if(storeCert != nullptr) {
      const bool certStoringOk = storeCert(certFile, certFile.size());
      if(!certStoringOk) {
      serverCertErrState.setError(ServerCertError::CERT_STORING_FAILED);
      }
    } else {
      serverCertErrState.setError(ServerCertError::CALLBACK_NULLPTR);
    }
  } else {
    serverCertErrState.setError(ServerCertError::CERT_FILE_EMPTY);
  }

  certFile.close();
  return serverCertErrState.getRawErrorState();
}