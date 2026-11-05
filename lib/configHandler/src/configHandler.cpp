#include "configHandler.hpp"

bool ConfigHandler::initialiseFileSystem(size_t& totalBytes, size_t& usedBytes, size_t& freeBytes) {
  const bool initFS = LittleFS.begin();
  if(!initFS) { return false; }
#ifdef ESP8266
  FSInfo fsInfo;
  LittleFS.info(fsInfo);
  totalBytes = fsInfo.totalBytes;
  usedBytes = fsInfo.usedBytes;
#elif defined(ESP32)
  totalBytes = LittleFS.totalBytes();
  usedBytes = LittleFS.usedBytes();
#else
  totalBytes = 0U;
  usedBytes = 0U;
#endif
  freeBytes = totalBytes - usedBytes;
  return true;
}

ConfigHandler::JsonLoadResult ConfigHandler::loadJsonFile(const char* filePath_P, JsonDocument& doc) {
  File file = LittleFS.open(FPSTR(filePath_P), "r");
  if(!file) { return JsonLoadResult::FileOpenFailed; }
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  return (err == DeserializationError::Code::Ok) ? JsonLoadResult::Ok : JsonLoadResult::ParseFailed;
}

ConfigHandler::WifiConfigErrorType ConfigHandler::getWifiConfig(char (&ssid)[maxWifiSsidSize], char (&password)[maxWifiPasswordSize]) {
  ErrorState<WifiConfigError, WifiConfigErrorType> wifiConfErrState;
  const char* const credPath = FileName::getMqttServerCredentialsLocation();

  JsonDocument wifiJson;
  const JsonLoadResult loadResult = loadJsonFile(credPath, wifiJson);
  if(loadResult == JsonLoadResult::FileOpenFailed) {
    wifiConfErrState.setError(WifiConfigError::FILE_OPEN_FAILED);
    return wifiConfErrState.getRawErrorState();
  }
  if(loadResult == JsonLoadResult::ParseFailed) {
    wifiConfErrState.setError(WifiConfigError::JSON_PARSING_ERROR);
    return wifiConfErrState.getRawErrorState();
  }

  JsonVariant ssidJsonVar = wifiJson[F("ssid")];
  JsonVariant passwordJsonVar = wifiJson[F("password")];
  const bool ssidKeyOk = ssidJsonVar.is<const char*>();
  const bool passwordKeyOk = passwordJsonVar.is<const char*>();
  if(!ssidKeyOk) { wifiConfErrState.setError(WifiConfigError::MISSING_SSID_KEY); }
  if(!passwordKeyOk) { wifiConfErrState.setError(WifiConfigError::MISSING_PWD_KEY); }
  if(!ssidKeyOk || !passwordKeyOk) { return wifiConfErrState.getRawErrorState(); }

  const JsonString ssidValue = ssidJsonVar.as<JsonString>();
  const JsonString passwordValue = passwordJsonVar.as<JsonString>();
  const size_t ssidLength = ssidValue.size();
  const size_t passLength = passwordValue.size();
  // JSON strings retain embedded NULs. Comparing their stored length with the C-string length
  // rejects those instead of silently connecting with only the prefix before the NUL.
  const bool ssidLengthValid = (ssidLength > 0U) && (ssidLength < maxWifiSsidSize) && (strnlen(ssidValue.c_str(), ssidLength) == ssidLength);
  const bool passLengthValid = (passLength > 0U) && (passLength < maxWifiPasswordSize) && (strnlen(passwordValue.c_str(), passLength) == passLength);
  if(!ssidLengthValid) { wifiConfErrState.setError(WifiConfigError::SSID_LENGTH_ERR); }
  if(!passLengthValid) { wifiConfErrState.setError(WifiConfigError::PWD_LENGTH_ERR); }
  if(ssidLengthValid && passLengthValid) {
    strlcpy(ssid, ssidValue.c_str(), maxWifiSsidSize);
    strlcpy(password, passwordValue.c_str(), maxWifiPasswordSize);
  }
  return wifiConfErrState.getRawErrorState();
}

ConfigHandler::ServerCredErrorType ConfigHandler::getServerCredentials(char (&mqttUserName)[maxMqttUserNameSize], char (&mqttPassword)[maxMqttPasswordSize], char (&mqttServerUrl)[maxMqttServerUrlSize], uint16_t& mqttServerPort) {
  ErrorState<ServerCredError, ServerCredErrorType> serverCredErrState;
  const char* const credPath = FileName::getMqttServerCredentialsLocation();

  JsonDocument serverCredJson;
  const JsonLoadResult loadResult = loadJsonFile(credPath, serverCredJson);
  if(loadResult == JsonLoadResult::FileOpenFailed) {
    serverCredErrState.setError(ServerCredError::FILE_OPEN_FAILED);
    return serverCredErrState.getRawErrorState();
  }
  if(loadResult == JsonLoadResult::ParseFailed) {
    serverCredErrState.setError(ServerCredError::JSON_PARSING_ERROR);
    return serverCredErrState.getRawErrorState();
  }

  JsonVariant mqttUserNameVar = serverCredJson[F("mqttUserName")];
  JsonVariant mqttPasswordVar = serverCredJson[F("mqttPassword")];
  JsonVariant mqttServerUrlVar = serverCredJson[F("mqttServerUrl")];
  JsonVariant mqttServerPortVar = serverCredJson[F("mqttServerPort")];
  const bool mqttUserNameKeyOk = mqttUserNameVar.is<const char*>();
  const bool mqttPasswordKeyOk = mqttPasswordVar.is<const char*>();
  const bool mqttServerUrlKeyOk = mqttServerUrlVar.is<const char*>();
  const bool mqttServerPortKeyOk = mqttServerPortVar.is<uint16_t>();
  if(!mqttUserNameKeyOk) { serverCredErrState.setError(ServerCredError::MISSING_MQTT_USER); }
  if(!mqttPasswordKeyOk) { serverCredErrState.setError(ServerCredError::MISSING_MQTT_PASS); }
  if(!mqttServerUrlKeyOk) { serverCredErrState.setError(ServerCredError::MISSING_MQTT_URL); }
  if(!mqttServerPortKeyOk) { serverCredErrState.setError(ServerCredError::MISSING_MQTT_PORT); }
  if(!mqttUserNameKeyOk || !mqttPasswordKeyOk || !mqttServerUrlKeyOk || !mqttServerPortKeyOk) {
    return serverCredErrState.getRawErrorState();
  }

  const JsonString mqttUserNameValue = mqttUserNameVar.as<JsonString>();
  const JsonString mqttPasswordValue = mqttPasswordVar.as<JsonString>();
  const JsonString mqttServerUrlValue = mqttServerUrlVar.as<JsonString>();
  const uint16_t mqttServerPortNum = mqttServerPortVar.as<uint16_t>();
  const size_t mqttUserNameLength = mqttUserNameValue.size();
  const size_t mqttPasswordLength = mqttPasswordValue.size();
  const size_t mqttServerUrlLength = mqttServerUrlValue.size();
  // Compare stored JSON lengths with C-string lengths so embedded NULs cannot shorten credentials.
  const bool mqttUserNameLengthValid = (mqttUserNameLength > 0U) && (mqttUserNameLength < maxMqttUserNameSize) && (strnlen(mqttUserNameValue.c_str(), mqttUserNameLength) == mqttUserNameLength);
  const bool mqttPasswordLengthValid = (mqttPasswordLength > 0U) && (mqttPasswordLength < maxMqttPasswordSize) && (strnlen(mqttPasswordValue.c_str(), mqttPasswordLength) == mqttPasswordLength);
  const bool mqttServerUrlLengthValid = (mqttServerUrlLength > 0U) && (mqttServerUrlLength < maxMqttServerUrlSize) && (strnlen(mqttServerUrlValue.c_str(), mqttServerUrlLength) == mqttServerUrlLength);
  const bool mqttServerPortNumValid = (mqttServerPortNum > 0U);
  if(!mqttUserNameLengthValid) { serverCredErrState.setError(ServerCredError::MQTT_USER_LENGTH_ERR); }
  if(!mqttPasswordLengthValid) { serverCredErrState.setError(ServerCredError::MQTT_PASS_LENGTH_ERR); }
  if(!mqttServerUrlLengthValid) { serverCredErrState.setError(ServerCredError::MQTT_URL_LENGTH_ERR); }
  if(!mqttServerPortNumValid) { serverCredErrState.setError(ServerCredError::MQTT_PORT_NUM_ERR); }
  if(!mqttUserNameLengthValid || !mqttPasswordLengthValid || !mqttServerUrlLengthValid || !mqttServerPortNumValid) {
    return serverCredErrState.getRawErrorState();
  }

  strlcpy(mqttUserName, mqttUserNameValue.c_str(), maxMqttUserNameSize);
  strlcpy(mqttPassword, mqttPasswordValue.c_str(), maxMqttPasswordSize);
  strlcpy(mqttServerUrl, mqttServerUrlValue.c_str(), maxMqttServerUrlSize);
  mqttServerPort = mqttServerPortNum;
  return serverCredErrState.getRawErrorState();
}
