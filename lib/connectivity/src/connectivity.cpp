#include "connectivity.hpp"
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include <time.h>
#include <ctype.h>
#if defined(ESP32)
  #include <esp_sntp.h>
#endif

Connectivity::Connectivity(NetworkManager& networkManager, void (*debugLedFunc)(bool state), void (*resetWdtFunc)()) :
  networkManager(networkManager),
  tcpClient(),
  mqttClient(tcpClient),
  networkState(true),
  mqttState(PubSubClient::State::CONNECTED),
  onlineState(true),
  deviceResetTimer(0U),
  debugLed(debugLedFunc),
  resetWdt(resetWdtFunc),
  reconnectTimer(0U),
#ifdef ESP8266
  serverCert{},
#endif
  haDiscovery(*this)
{}

bool Connectivity::init() { // NOLINT(readability-function-cognitive-complexity)
  { // Initialise the file system.
    delay(10U);
    uint32_t totalBytes = 0U;
    uint32_t usedBytes = 0U;
    uint32_t freeBytes = 0U;
    const bool initFS = ConfigHandler::initialiseFileSystem(totalBytes, usedBytes, freeBytes);
    Logger::get().printf_P(PSTR("[FS] File system initialisation: %s\r\n"), Str::getStateStr(initFS));
    if(!initFS) { return false; }
    Logger::get().printf_P(PSTR("  Total bytes: %u\r\n  Used bytes: %u\r\n  Free bytes: %u\r\n"), totalBytes, usedBytes, freeBytes);
  }
  { // Start network interface.
    resetWatchdogTimer();
    const uint16_t connResult = networkManager.connect();
    const bool connResultOk = (connResult == 0U);
    Logger::get().printf_P(PSTR("[NETWORK] Connection: %s\r\n"), Str::getStateStr(connResultOk));
    if(!connResultOk) {
      Logger::get().printf_P(PSTR("  Code: %hu\r\n"), connResult);
      return false;
    }
  }
  { // Set time via NTP, as required for x.509 validation.
    resetWatchdogTimer();
    const bool ntpSynced = syncNtpTime();
    Logger::get().printf_P(PSTR("[NTP] Synchronisation: %s\r\n"), Str::getStateStr(ntpSynced));
    if(!ntpSynced) { return false; }
    char dateTimeStr[dateTimeStrBufSize] = {'\0'};
    const bool dateTimeValid = getIsoTimeString(dateTimeStr);
    if(dateTimeValid) {
      Logger::get().printf_P(PSTR("[NTP] UTC ISO time: %s\r\n"), dateTimeStr);
    } else {
      Logger::get().printf_P(PSTR("[NTP] Retrieving local time failed!\r\n"));
      return false;
    }
  }
  { // Get MQTT server credentials.
    const uint16_t credResult = ConfigHandler::getServerCredentials(mqttCredentials.userName, mqttCredentials.password, mqttCredentials.serverName, mqttCredentials.serverPort);
    const bool credResultOk = (credResult == 0U);
    Logger::get().printf_P(PSTR("[MQTT] Server credentials: %s\r\n"), Str::getStateStr(credResultOk));
    if(!credResultOk) {
      Logger::get().printf_P(PSTR("  Code: %hu\r\n"), credResult);
      return false;
    }
  }
  { // Setup MQTT topics.
    uint8_t mac[6] = { 0U };
    if(!networkManager.getMacAddress(mac)) { return false; }
    const char* pioEnv = Build::getPioEnv();
    if(pioEnv == nullptr) { return false; }
    const char* underscore = strchr(pioEnv, '_');
    if(underscore == nullptr || *(underscore + 1) == '\0') {
      Logger::get().printf_P(PSTR("[MQTT] Device ID is invalid!\r\n"));
      return false;
    }
    const char* deviceId = underscore + 1;
    char macHex[macHexLen + 1U] = { '\0' };
    const int32_t macHexSize = snprintf_P(macHex, sizeof(macHex), PSTR("%02x%02x%02x%02x%02x%02x"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const bool macHexValid = (macHexSize == static_cast<int32_t>(macHexLen));
    Logger::get().printf_P(PSTR("[MQTT] MAC hex: %s\r\n"), Str::getStateStr(macHexValid));
    if(!macHexValid) { return false; }
    haDiscovery.buildDeviceName(mac, deviceId);
    const int32_t clientNameSize = snprintf_P(mqttCredentials.clientName, sizeof(mqttCredentials.clientName), mqttClientName, deviceId, macHex);
    const int32_t senderTopicSize = snprintf_P(mqttCredentials.senderTopic, sizeof(mqttCredentials.senderTopic), mqttOutTopic, macHex);
    const int32_t receiverTopicSize = snprintf_P(mqttCredentials.receiverTopic, sizeof(mqttCredentials.receiverTopic), mqttInTopic, macHex);
    const bool clientNameValid = (clientNameSize >= 0 && clientNameSize < static_cast<int32_t>(sizeof(mqttCredentials.clientName)));
    const bool senderTopicValid = (senderTopicSize >= 0 && senderTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.senderTopic)));
    const bool receiverTopicValid = (receiverTopicSize >= 0 && receiverTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.receiverTopic)));
    const int32_t availTopicSize = snprintf_P(mqttCredentials.availabilityTopic, sizeof(mqttCredentials.availabilityTopic), mqttAvailTopic, mqttCredentials.senderTopic);
    const bool availTopicValid = (availTopicSize >= 0 && availTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.availabilityTopic)));
    Logger::get().printf_P(PSTR("[MQTT] Client name: %s\r\n"), clientNameValid ? mqttCredentials.clientName : Str::getErrStr());
    Logger::get().printf_P(PSTR("[MQTT] Sender topic: %s\r\n"), senderTopicValid ? mqttCredentials.senderTopic : Str::getErrStr());
    Logger::get().printf_P(PSTR("[MQTT] Receiver topic: %s\r\n"), receiverTopicValid ? mqttCredentials.receiverTopic : Str::getErrStr());
    Logger::get().printf_P(PSTR("[MQTT] Availability topic: %s\r\n"), availTopicValid ? mqttCredentials.availabilityTopic : Str::getErrStr());
    if(!clientNameValid || !senderTopicValid || !receiverTopicValid || !availTopicValid) { return false; }
  }
  { // Open certificate.
    const uint8_t certResult = ConfigHandler::getServerCert([this](Stream& certFile, size_t certFileSize) -> bool {
#ifdef ESP8266
      serverCert.emplace(certFile, certFileSize);
      if(serverCert.has_value()) {
        tcpClient.setTrustAnchors(&serverCert.value());
        tcpClient.setTimeout(Time::secToMs(5U));
      }
      return serverCert.has_value();
#elif defined ESP32
      tcpClient.setTimeout(5U);  // seconds; ESP32 crypto is fast (TLS handshake < 3s typical)
      return tcpClient.loadCACert(certFile, certFileSize);
#endif
    });
    const bool certResultOk = (certResult == 0U);
    Logger::get().printf_P(PSTR("[TCP] Server certificate setup: %s\r\n"), Str::getStateStr(certResultOk));
    if(!certResultOk) {
      Logger::get().printf_P(PSTR("  Code: %hu\r\n"), certResult);
      return false;
    }
  }
  // Setup MQTT client.
  mqttClient.setServer(mqttCredentials.serverName, mqttCredentials.serverPort);
  mqttClient.setCallback([this](const char* topic, const uint8_t* payload, uint32_t length) -> void {
    if((topic == nullptr) || (payload == nullptr) || (length == 0U)) { return; }
    const char* subtopic = topic + subtopicOffset;
    if(!MqttBase::isSubtopicValid(subtopic)) { return; }
    for(MqttBase* currentMessageHandler = handlerListHead; currentMessageHandler != nullptr; currentMessageHandler = currentMessageHandler->getNextHandler()) {
      if(strcmp(currentMessageHandler->getSubtopic(), subtopic) == 0) {
        JsonDocument payloadJson;
        DeserializationError parsingError = deserializeJson(payloadJson, payload, length);
        if(parsingError != DeserializationError::Code::Ok) {
          Logger::get().printf_P(PSTR("[MQTT] Parsing failed for: \"%s\" -> %s\r\n"),
            currentMessageHandler->getSubtopic(), reinterpret_cast<const char*>(parsingError.f_str()));
          return;
        }
        currentMessageHandler->messageArrivedCallback(payloadJson);
        return;
      }
    }
    Logger::get().printf_P(PSTR("[MQTT] No handler -> \"%s\"\r\n"), subtopic);
  });
  // List message handlers.
  Logger::get().printf_P(PSTR("[MQTT] Message handlers:\r\n"));
  uint8_t handlerIndex = 0U;
  for(MqttBase* h = handlerListHead; h != nullptr; h = h->getNextHandler()) {
    Logger::get().printf_P(PSTR("  %hhu. %s\r\n"), handlerIndex++, h->getSubtopic());
  }

  if(!connectToMqttServer()) { return false; }
  { // Publish retained device info once at startup.
    char infoTopic[infoTopicBufSize] = { '\0' };
    const int32_t infoTopicSize = snprintf_P(infoTopic, sizeof(infoTopic), mqttInfoTopic, mqttCredentials.senderTopic);
    char infoPayload[infoPayloadBufSize] = { '\0' };
    const int32_t infoPayloadSize = snprintf_P(infoPayload, sizeof(infoPayload), mqttInfoPayload,
      Build::getFwVersion(), Build::getGitHash(), Build::getGitDirty(), ResetHandler::getResetReason());
    const bool infoTopicValid   = (infoTopicSize   >= 0 && infoTopicSize   < static_cast<int32_t>(sizeof(infoTopic)));
    const bool infoPayloadValid = (infoPayloadSize >= 0 && infoPayloadSize < static_cast<int32_t>(sizeof(infoPayload)));
    if(!infoTopicValid || !infoPayloadValid) { return false; }
    const bool infoResult = mqttClient.publish(infoTopic, infoPayload, true);
    Logger::get().printf_P(PSTR("[MQTT] Device info: %s\r\n"), Str::getStateStr(infoResult));
  }
  return true;
}

bool Connectivity::connectToMqttServer() { // NOLINT(readability-convert-member-functions-to-static)
  const bool mqttConResult = mqttClient.connect(
    mqttCredentials.clientName, mqttCredentials.userName, mqttCredentials.password,
    mqttCredentials.availabilityTopic, 1U, true, availOfflinePayload);
  Logger::get().printf_P(PSTR("[MQTT] Connecting to: %s:%hu %s\r\n  State: %s\r\n"),
    mqttCredentials.serverName, mqttCredentials.serverPort, Str::getStateStr(mqttConResult), getMqttStatusStr(mqttClient.state()));
  if(!mqttConResult) { return false; }
  const bool subResult = mqttClient.subscribe(mqttCredentials.receiverTopic, 1U);
  Logger::get().printf_P(PSTR("[MQTT] Subscription: %s\r\n"), Str::getStateStr(subResult));
  if(!subResult) { return false; }
  const bool availResult = mqttClient.publish(mqttCredentials.availabilityTopic, availOnlinePayload, true);
  Logger::get().printf_P(PSTR("[MQTT] Availability: %s\r\n"), Str::getStateStr(availResult));
  if(!availResult) { return false; }
  (void)haDiscovery.publishConnectivity();
  for(MqttBase* h = handlerListHead; h != nullptr; h = h->getNextHandler()) {
    if(!h->publishDiscovery()) {
      Logger::get().printf_P(PSTR("[MQTT] Discovery failed: %s\r\n"), h->getSubtopic());
    }
  }
  return true;
}

bool Connectivity::run() {
  const uint32_t actualTime = millis();
  const bool actualNetworkState = networkManager.isNetworkAvailable();
  if(actualNetworkState != networkState) {
    networkState = actualNetworkState;
    if(networkState) {
      connectToMqttServer();
    } else {
      (void)mqttClient.publish(mqttCredentials.availabilityTopic, availOfflinePayload, true);
      mqttClient.disconnect();
    }
  }
  const PubSubClient::State actualMqttState = mqttClient.state();
  if(mqttState != actualMqttState) {
    Logger::get().printf_P(PSTR("[MQTT] Status changed: %s -> %s\r\n"), getMqttStatusStr(mqttState), getMqttStatusStr(actualMqttState));
    mqttState = actualMqttState;
  }

  if(mqttClient.loop()) {
    reconnectTimer = actualTime;
  } else {
    if((mqttState != PubSubClient::State::CONNECTED) && networkState) {
      if(Time::hasElapsed(actualTime, reconnectTimer, reconnectTime)) {
        reconnectTimer = actualTime;
        connectToMqttServer();
      }
    }
  }

  const bool actualOnlineState = networkState && (mqttState == PubSubClient::State::CONNECTED);
  if(actualOnlineState) {
    deviceResetTimer = actualTime;
  }
  if(actualOnlineState != onlineState) {
    onlineState = actualOnlineState;
    if(debugLed != nullptr) { debugLed(onlineState); }
    Logger::get().printf_P(PSTR("[RUN] Device is: %s\r\n"), onlineState ? PSTR("ONLINE") : PSTR("OFFLINE"));
  }

  if(Time::hasElapsed(actualTime, deviceResetTimer, deviceResetTime)) {
    Logger::get().printf_P(PSTR("[RUN] Device is offline since: %ums\r\n"), (actualTime - deviceResetTimer));
    ResetHandler::restartMCU();
  }
  return true;
}

bool Connectivity::sendMqttMessage(const char* subTopic, const char* payload) {
  if(subTopic == nullptr || payload == nullptr) { return false; }
  char actualTopic[sizeof(mqttCredentials.senderTopic) + MqttBase::getSubtopicSize()] = { '\0' };
  strlcpy(actualTopic, mqttCredentials.senderTopic, sizeof(actualTopic));
  const size_t actualTopicLen = strlcat(actualTopic, subTopic, sizeof(actualTopic));
  if(actualTopicLen >= sizeof(actualTopic)) { return false; }
  return mqttClient.publish(actualTopic, payload);
}

bool Connectivity::syncNtpTime() {
  const char* ntpServers[] = {"0.hu.pool.ntp.org", "1.hu.pool.ntp.org", "2.hu.pool.ntp.org"};
  constexpr uint32_t timeoutMs = Time::secToMs(5U);

  Logger::get().printf_P(PSTR("[NTP] Synchronising...\r\n"));
  configTime(0, 0, ntpServers[0], ntpServers[1], ntpServers[2]);

  const uint32_t startMs = millis();
#if defined(ESP32)
  while(sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET) {
#else
  constexpr time_t minValidTime = 8L * 3600L * 2L;
  while(time(nullptr) < minValidTime) {
#endif
    if(Time::hasElapsed(millis(), startMs, timeoutMs)) { return false; }
    yield();
  }
  return true;
}

bool Connectivity::getIsoTimeString(char (&dateTimeBuffer)[dateTimeStrBufSize]) {
  const time_t currentTime = time(nullptr);
  if(currentTime == -1) { return false; }           // Check if time retrieval failed.
  const tm* utcTimeInfo = gmtime(&currentTime);     // Convert time to UTC time structure.
  if(utcTimeInfo == nullptr) { return false; }      // Check if time conversion failed.
  const size_t formattedSize = strftime(dateTimeBuffer, sizeof(dateTimeBuffer), "%Y-%m-%dT%H:%M:%SZ", utcTimeInfo);
  return (formattedSize > 0U && formattedSize < sizeof(dateTimeBuffer));
}

void Connectivity::HADiscovery::getSwVersionStr(char (&buf)[swVersionBufSize]) {
  (void)snprintf_P(buf, sizeof(buf), PSTR("%hu (%08x)"), Build::getFwVersion(), Build::getGitHash());
}

void Connectivity::HADiscovery::buildDeviceName(const uint8_t mac[6], const char* deviceId) {
  memset(deviceName, '\0', sizeof(deviceName));
  for(uint8_t i = 0U; i < (deviceNameBufSize - 8U) && deviceId[i] != '\0'; ++i) {
    deviceName[i] = (deviceId[i] == '_')
      ? ' ' : static_cast<char>(toupper(static_cast<unsigned char>(deviceId[i])));
  }
  const uint8_t prefixLen = static_cast<uint8_t>(strnlen(deviceName, deviceNameBufSize));
  deviceName[prefixLen] = ' ';
  (void)snprintf_P(deviceName + prefixLen + 1U, 7U, PSTR("%02X%02X%02X"), mac[3], mac[4], mac[5]);
  Logger::get().printf_P(PSTR("[MQTT] Device name: %s\r\n"), deviceName);
}

bool Connectivity::HADiscovery::publishConnectivity() { // NOLINT(readability-convert-member-functions-to-static)
  constexpr EntityConfig config = {EntityType::binary_sensor, connEntityFields, false};
  // Advance past the "%s" prefix of mqttAvailTopic to get the "availability" subtopic string.
  const bool result = publishEntity(mqttAvailTopic + (sizeof("%s") - 1U), config);
  Logger::get().printf_P(PSTR("[MQTT] Connection discovery: %s\r\n"), Str::getStateStr(result));
  return result;
}

bool Connectivity::HADiscovery::publishEntity(const char* subtopic, const EntityConfig& config) {
  const char* haType = getTypeStr(config.type);
  if(subtopic == nullptr || haType == nullptr || config.entityFields == nullptr) { return false; }
  char swVersion[swVersionBufSize] = { '\0' };
  getSwVersionStr(swVersion);
  char discTopic[discoveryTopicBufSize] = { '\0' };
  const int32_t topicSize = snprintf_P(discTopic, sizeof(discTopic), mqttDiscoveryTopic,
    haType, conn.mqttCredentials.clientName, subtopic);
  if(topicSize < 0 || topicSize >= static_cast<int32_t>(sizeof(discTopic))) { return false; }
  // Build topic base: senderTopic for state_topic, receiverBase for command_topic.
  char topicBase[receiverTopicBufSize] = { '\0' };
  if(config.isCommandTopic) {
    strlcpy(topicBase, conn.mqttCredentials.receiverTopic, subtopicOffset + 1U);
  } else {
    strlcpy(topicBase, conn.mqttCredentials.senderTopic, sizeof(topicBase));
  }
  const char* topicField = config.isCommandTopic ? topicFieldCmd : topicFieldState;
  char payload[discoveryPayloadBufSize] = { '\0' };
  const int32_t payloadSize = snprintf_P(payload, sizeof(payload), mqttDiscoveryTemplate,
    conn.mqttCredentials.clientName, subtopic, config.entityFields,
    topicField, topicBase, subtopic,
    conn.mqttCredentials.availabilityTopic, conn.mqttCredentials.clientName,
    deviceName, swVersion);
  if(payloadSize < 0 || payloadSize >= static_cast<int32_t>(sizeof(payload))) { return false; }
  return conn.mqttClient.publish(discTopic, payload, true);
}

bool Connectivity::publishEntityDiscovery(const char* subtopic, const HADiscovery::EntityConfig& config) {
  return haDiscovery.publishEntity(subtopic, config);
}

bool Connectivity::registerCallback(MqttBase* mqttBasePtr) { // NOLINT(readability-convert-member-functions-to-static)
  if(mqttBasePtr == nullptr) { return false; } // NOLINT(readability-simplify-boolean-expr)
  if(handlerListTail != nullptr) {
    handlerListTail->setNextHandler(mqttBasePtr);
  } else {
    handlerListHead = mqttBasePtr;
  }
  handlerListTail = mqttBasePtr;
  return true;
}

const char* Connectivity::getMqttStatusStr(PubSubClient::State status) {  // NOLINT(readability-convert-member-functions-to-static)
  switch(status) {
    case PubSubClient::State::CONNECTION_TIMEOUT:     { return mqttConnectionTimeoutStr; }
    case PubSubClient::State::CONNECTION_LOST:        { return mqttConnectionLostStr; }
    case PubSubClient::State::CONNECT_FAILED:         { return mqttConnectFailedStr; }
    case PubSubClient::State::DISCONNECTED:           { return mqttDisconnectedStr; }
    case PubSubClient::State::CONNECTED:              { return mqttConnectedStr; }
    case PubSubClient::State::CONNECT_BAD_PROTOCOL:   { return mqttConnectBadProtocolStr; }
    case PubSubClient::State::CONNECT_BAD_CLIENT_ID:  { return mqttConnectBadClientIdStr; }
    case PubSubClient::State::CONNECT_UNAVAILABLE:    { return mqttConnectUnavailableStr; }
    case PubSubClient::State::CONNECT_BAD_CREDENTIALS: { return mqttConnectBadCredentialsStr; }
    case PubSubClient::State::CONNECT_UNAUTHORIZED:   { return mqttConnectUnauthorizedStr; }
    default:                                        { return mqttUnknownStatusStr; }
  }
}