#include "configHandler.hpp"

bool ConfigHandler::loadJsonFile(const char* filePath_P, JsonDocument& doc) {
  File file = LittleFS.open(FPSTR(filePath_P), "r");
  if(!file) { return false; }
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  return err == DeserializationError::Code::Ok;
}

bool ConfigHandler::initialiseFileSystem(size_t& totalBytes, size_t& usedBytes, size_t& freeBytes) { // NOLINT(readability-convert-member-functions-to-static)
  const bool initFS = LittleFS.begin();
  if(!initFS) { return false; } // NOLINT(readability-simplify-boolean-expr)
#if defined(ESP8266)
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

ConfigHandler::WifiConfigErrorType ConfigHandler::getWifiConfig(char (&ssid)[maxWifiSsidSize], char (&password)[maxWifiPasswordSize]) {
  ErrorState<WifiConfigError, WifiConfigErrorType> wifiConfErrState;
  const char* const credPath = FileName::getMqttServerCredentialsLocation();

  File wifiFile = LittleFS.open(FPSTR(credPath), "r");
  if(!wifiFile) {
    wifiConfErrState.setError(WifiConfigError::FILE_OPEN_FAILED);
    return wifiConfErrState.getRawErrorState();
  }

  JsonDocument wifiJson;
  DeserializationError deserializationError = deserializeJson(wifiJson, wifiFile);
  wifiFile.close();

  const bool isDeserializationSuccessful = (deserializationError == DeserializationError::Code::Ok);
  if(isDeserializationSuccessful) {
    JsonVariant ssidJsonVar = wifiJson[F("ssid")];
    JsonVariant passwordJsonVar = wifiJson[F("password")];
    const bool ssidKeyOk = ssidJsonVar.is<const char*>();
    const bool passwordKeyOk = passwordJsonVar.is<const char*>();
    if(!ssidKeyOk) { wifiConfErrState.setError(WifiConfigError::MISSING_SSID_KEY); }
    if(!passwordKeyOk) { wifiConfErrState.setError(WifiConfigError::MISSING_PWD_KEY); }
    if(!ssidKeyOk || !passwordKeyOk) {
      return wifiConfErrState.getRawErrorState();
    }
    const char* ssidJsonPtr = ssidJsonVar.as<const char*>();
    const char* passJsonPtr = passwordJsonVar.as<const char*>();
    const uint8_t ssidLength = static_cast<uint8_t>(strnlen(ssidJsonPtr, maxWifiSsidSize));
    const uint8_t passLength = static_cast<uint8_t>(strnlen(passJsonPtr, maxWifiPasswordSize));
    const bool ssidLengthValid = (ssidLength > 0U) && (ssidLength < maxWifiSsidSize);
    const bool passLengthValid = (passLength > 0U) && (passLength < maxWifiPasswordSize);
    if(!ssidLengthValid) { wifiConfErrState.setError(WifiConfigError::SSID_LENGTH_ERR); }
    if(!passLengthValid) { wifiConfErrState.setError(WifiConfigError::PWD_LENGTH_ERR); }
    if(ssidLengthValid && passLengthValid) {
      strlcpy(ssid, ssidJsonPtr, maxWifiSsidSize);
      strlcpy(password, passJsonPtr, maxWifiPasswordSize);
    }
  } else {
    wifiConfErrState.setError(WifiConfigError::JSON_PARSING_ERROR);
  }

  return wifiConfErrState.getRawErrorState();
}

ConfigHandler::ServerCredErrorType ConfigHandler::getServerCredentials(char (&mqttUserName)[maxMqttUserNameSize],
  char (&mqttPassword)[maxMqttPasswordSize], char (&mqttServerUrl)[maxMqttServerUrlSize], uint16_t &mqttServerPort) {
  ErrorState<ServerCredError, ServerCredErrorType> serverCredErrState;
  const char* const credPath = FileName::getMqttServerCredentialsLocation();

  File credFile = LittleFS.open(FPSTR(credPath), "r");
  if(!credFile) {
    serverCredErrState.setError(ServerCredError::FILE_OPEN_FAILED);
    return serverCredErrState.getRawErrorState();
  }

  JsonDocument serverCredJson;
  DeserializationError deserializationError = deserializeJson(serverCredJson, credFile);
  credFile.close();

  if(deserializationError != DeserializationError::Code::Ok) {
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

  const char* mqttUserNamePtr = mqttUserNameVar.as<const char*>();
  const char* mqttPasswordPtr = mqttPasswordVar.as<const char*>();
  const char* mqttServerUrlPtr = mqttServerUrlVar.as<const char*>();
  const uint16_t mqttServerPortNum = mqttServerPortVar.as<uint16_t>();
  const uint8_t mqttUserNameLength = static_cast<uint8_t>(strnlen(mqttUserNamePtr, maxMqttUserNameSize));
  const uint8_t mqttPasswordLength = static_cast<uint8_t>(strnlen(mqttPasswordPtr, maxMqttPasswordSize));
  const uint8_t mqttServerUrlLength = static_cast<uint8_t>(strnlen(mqttServerUrlPtr, maxMqttServerUrlSize));
  const bool mqttUserNameLengthValid = (mqttUserNameLength > 0U) && (mqttUserNameLength < maxMqttUserNameSize);
  const bool mqttPasswordLengthValid = (mqttPasswordLength > 0U) && (mqttPasswordLength < maxMqttPasswordSize);
  const bool mqttServerUrlLengthValid = (mqttServerUrlLength > 0U) && (mqttServerUrlLength < maxMqttServerUrlSize);
  const bool mqttServerPortNumValid = (mqttServerPortNum > 0U);
  if(!mqttUserNameLengthValid) { serverCredErrState.setError(ServerCredError::MQTT_USER_LENGTH_ERR); }
  if(!mqttPasswordLengthValid) { serverCredErrState.setError(ServerCredError::MQTT_PASS_LENGTH_ERR); }
  if(!mqttServerUrlLengthValid) { serverCredErrState.setError(ServerCredError::MQTT_URL_LENGTH_ERR); }
  if(!mqttServerPortNumValid) { serverCredErrState.setError(ServerCredError::MQTT_PORT_NUM_ERR); }
  if(!mqttUserNameLengthValid || !mqttPasswordLengthValid || !mqttServerUrlLengthValid || !mqttServerPortNumValid) {
    return serverCredErrState.getRawErrorState();
  }

  strlcpy(mqttUserName, mqttUserNamePtr, maxMqttUserNameSize);
  strlcpy(mqttPassword, mqttPasswordPtr, maxMqttPasswordSize);
  strlcpy(mqttServerUrl, mqttServerUrlPtr, maxMqttServerUrlSize);
  mqttServerPort = mqttServerPortNum;
  return serverCredErrState.getRawErrorState();
}
