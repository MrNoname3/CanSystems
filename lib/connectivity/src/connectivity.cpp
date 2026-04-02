#include "connectivity.hpp"
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include <time.h>

Connectivity::Connectivity(NetworkManager& networkManager, void (*debugLedFunc)(bool state), void (*resetWdtFunc)()) :
  networkManager(networkManager),
  tcpClient(),
  mqttClient(tcpClient),
  networkState(true),
  mqttState(MQTT_CONNECTED),
  onlineState(true),
  deviceResetTimer(0U),
  debugLed(debugLedFunc),
  resetWdt(resetWdtFunc),
  subtopicOffset(0U),
  reconnectTimer(0U),
#ifdef ESP8266
  serverCert{},
#endif
  messageHandlerList{}
{}

bool Connectivity::init() {
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
    syncNtpTime();
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
    const int32_t clientNameSize = snprintf_P(mqttCredentials.clientName, sizeof(mqttCredentials.clientName), mqttClientName, deviceId, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const int32_t senderTopicSize = snprintf_P(mqttCredentials.senderTopic, sizeof(mqttCredentials.senderTopic), mqttOutTopic, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], PSTR("%s"));
    const int32_t receiverTopicSize = snprintf_P(mqttCredentials.receiverTopic, sizeof(mqttCredentials.receiverTopic), mqttInTopic, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const bool clientNameValid = (clientNameSize >= 0 && clientNameSize < static_cast<int32_t>(sizeof(mqttCredentials.clientName)));
    const bool senderTopicValid = (senderTopicSize >= 0 && senderTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.senderTopic)));
    const bool receiverTopicValid = (receiverTopicSize >= 0 && receiverTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.receiverTopic)));
    Logger::get().printf_P(PSTR("[MQTT] Client name: %s\r\n"), clientNameValid ? mqttCredentials.clientName : Str::getErrStr());
    Logger::get().printf_P(PSTR("[MQTT] Sender topic: %s\r\n"), senderTopicValid ? mqttCredentials.senderTopic : Str::getErrStr());
    Logger::get().printf_P(PSTR("[MQTT] Receiver topic: %s\r\n"), receiverTopicValid? mqttCredentials.receiverTopic : Str::getErrStr());
    if(!clientNameValid || !senderTopicValid || !receiverTopicValid) { return false; }
    subtopicOffset = senderTopicSize - 2U;
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
      tcpClient.setTimeout(10U);
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
    if((topic == nullptr) || (payload == nullptr) || (length ==  0U)) { return; }
    const char* subtopic = topic + subtopicOffset;
    if(!MqttBase::isSubtopicValid(subtopic)) { return; }
    for(const auto &currentMessageHandler : messageHandlerList) {
      if(currentMessageHandler == nullptr) { continue; }
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
  for(std::size_t i = 0U; i < messageHandlerList.size(); ++i) {
    if(messageHandlerList[i] != nullptr) {
      Logger::get().printf_P(PSTR("  %zu. %s\r\n"), i, messageHandlerList[i]->getSubtopic());
    } else {
      Logger::get().printf_P(PSTR("  %zu. No object here!\r\n"), i);
    }
  }

  return connectToMqttServer();
}

bool Connectivity::connectToMqttServer() { // NOLINT(readability-convert-member-functions-to-static)
  const bool mqttConResult = mqttClient.connect(mqttCredentials.clientName, mqttCredentials.userName, mqttCredentials.password);
  Logger::get().printf_P(PSTR("[MQTT] Connecting to: %s:%hu %s\r\n  State: %s\r\n"),
    mqttCredentials.serverName, mqttCredentials.serverPort, Str::getStateStr(mqttConResult), getMqttStatusStr(mqttClient.state()));
  if(!mqttConResult) { return false; }
  const bool subResult = mqttClient.subscribe(mqttCredentials.receiverTopic, 1U);
  Logger::get().printf_P(PSTR("[MQTT] Subscription: %s\r\n"), Str::getStateStr(subResult));
  return subResult;
}

bool Connectivity::run() {
  const uint32_t actualTime = millis();
  const bool actualNetworkState = networkManager.isNetworkAvailable();
  if(actualNetworkState != networkState) {
    networkState = actualNetworkState;
    if(networkState) {
      connectToMqttServer();
    } else {
      mqttClient.disconnect();
    }
  }
  const int8_t actualMqttState = mqttClient.state();
  if(mqttState != actualMqttState) {
    Logger::get().printf_P(PSTR("[MQTT] Status changed: %s -> %s\r\n"), getMqttStatusStr(mqttState), getMqttStatusStr(actualMqttState));
    mqttState = actualMqttState;
  }

  if(mqttClient.loop()) {
    reconnectTimer = millis();
  } else {
    if((mqttState != MQTT_CONNECTED) && networkState) {
      if(Time::hasElapsed(actualTime, reconnectTimer, reconnectTime)) {
        reconnectTimer = millis();
        connectToMqttServer();
      }
    }
  }

  const bool actualOnlineState = networkState && (mqttState == MQTT_CONNECTED);
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
  if(subTopic == nullptr || payload == nullptr ) { return false; }
  char actualTopic[sizeof(mqttCredentials.senderTopic) + MqttBase::getSubtopicSize()];
  const int32_t actualTopicSize = snprintf_P(actualTopic, sizeof(actualTopic), mqttCredentials.senderTopic, subTopic);
  const bool actualTopicValid = (actualTopicSize >= 0 && actualTopicSize < static_cast<int32_t>(sizeof(actualTopic)));
  if(!actualTopicValid) { return false; }
  return mqttClient.publish(actualTopic, payload);
}

void Connectivity::syncNtpTime() {
  const char* ntpServers[] = {"0.hu.pool.ntp.org", "1.hu.pool.ntp.org", "2.hu.pool.ntp.org"};
  constexpr time_t minValidTime = 8L * 3600L * 2L;  // Minimum valid epoch time (arbitrary example)

  Logger::get().printf_P(PSTR("[NTP] Synchronising...\r\n"));
  configTime(0, 0, ntpServers[0], ntpServers[1], ntpServers[2]);
  time_t currentTime = time(nullptr);
  while(currentTime < minValidTime) {
    yield();
    currentTime = time(nullptr);
  }
}

bool Connectivity::getIsoTimeString(char (&dateTimeBuffer)[dateTimeStrBufSize]) {
  memset(dateTimeBuffer, '\0', sizeof(dateTimeBuffer));
  const time_t currentTime = time(nullptr);
  if(currentTime == -1) { return false; }           // Check if time retrieval failed.
  const tm* utcTimeInfo = gmtime(&currentTime);     // Convert time to UTC time structure.
  if(utcTimeInfo == nullptr) { return false; }      // Check if time conversion failed.
  const size_t formattedSize = strftime(dateTimeBuffer, sizeof(dateTimeBuffer), "%Y-%m-%dT%H:%M:%SZ", utcTimeInfo);
  return (formattedSize > 0U && formattedSize < sizeof(dateTimeBuffer));
}

bool Connectivity::registerCallback(MqttBase* mqttBasePtr) { // NOLINT(readability-convert-member-functions-to-static)
  if(mqttBasePtr != nullptr) { messageHandlerList.push_back(mqttBasePtr); }
  return mqttBasePtr != nullptr;
}

const char* Connectivity::getMqttStatusStr(int8_t status) {
  switch(status) {
    case MQTT_CONNECTION_TIMEOUT: { return mqttConnectionTimeoutStr; } break;
    case MQTT_CONNECTION_LOST: { return mqttConnectionLostStr; } break;
    case MQTT_CONNECT_FAILED: { return mqttConnectFailedStr; } break;
    case MQTT_DISCONNECTED: { return mqttDisconnectedStr; } break;
    case MQTT_CONNECTED: { return mqttConnectedStr; } break;
    case MQTT_CONNECT_BAD_PROTOCOL: { return mqttConnectBadProtocolStr; } break;
    case MQTT_CONNECT_BAD_CLIENT_ID: { return mqttConnectBadClientIdStr; } break;
    case MQTT_CONNECT_UNAVAILABLE: { return mqttConnectUnavailableStr; } break;
    case MQTT_CONNECT_BAD_CREDENTIALS: { return mqttConnectBadCredentialsStr; } break;
    case MQTT_CONNECT_UNAUTHORIZED: { return mqttConnectUnauthorizedStr; } break;
    default: { return mqttUnknownStatusStr; } break;
  }
}