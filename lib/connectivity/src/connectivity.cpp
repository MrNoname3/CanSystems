#include "connectivity.hpp"
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include <ArduinoJson.h>                                            /// Handle JSON files.

const char Connectivity::BASE_TOPIC[] PROGMEM               = "iot";
const char Connectivity::SENDER_TOPIC[] PROGMEM             = "dtos";
const char Connectivity::RECEIVER_TOPIC[] PROGMEM           = "stod";
const char Connectivity::INIT_PREFIX[] PROGMEM              = "[INIT] ";
const char Connectivity::NTP_PREFIX[] PROGMEM               = "[NTP] ";
const char Connectivity::TCP_PREFIX[] PROGMEM               = "[TCP] ";
const char Connectivity::MQTT_PREFIX[] PROGMEM              = "[MQTT] ";
const char Connectivity::RUN_PREFIX[] PROGMEM               = "[RUN] ";

const char Connectivity::MQTT_CONNECTION_TIMEOUT_STR[] PROGMEM      = "MQTT_CONNECTION_TIMEOUT";
const char Connectivity::MQTT_CONNECTION_LOST_STR[] PROGMEM         = "MQTT_CONNECTION_LOST";
const char Connectivity::MQTT_CONNECT_FAILED_STR[] PROGMEM          = "MQTT_CONNECT_FAILED";
const char Connectivity::MQTT_DISCONNECTED_STR[] PROGMEM            = "MQTT_DISCONNECTED";
const char Connectivity::MQTT_CONNECTED_STR[] PROGMEM               = "MQTT_CONNECTED";
const char Connectivity::MQTT_CONNECT_BAD_PROTOCOL_STR[] PROGMEM    = "MQTT_CONNECT_BAD_PROTOCOL";
const char Connectivity::MQTT_CONNECT_BAD_CLIENT_ID_STR[] PROGMEM   = "MQTT_CONNECT_BAD_CLIENT_ID";
const char Connectivity::MQTT_CONNECT_UNAVAILABLE_STR[] PROGMEM     = "MQTT_CONNECT_UNAVAILABLE";
const char Connectivity::MQTT_CONNECT_BAD_CREDENTIALS_STR[] PROGMEM = "MQTT_CONNECT_BAD_CREDENTIALS";
const char Connectivity::MQTT_CONNECT_UNAUTHORIZED_STR[] PROGMEM    = "MQTT_CONNECT_UNAUTHORIZED";
const char Connectivity::MQTT_UNKNOWN_STATUS_STR[] PROGMEM          = "MQTT_UNKNOWN_STATUS";

Connectivity::Connectivity(HardwareSerial& serial, DebugLedHandler& debugLed, NetworkManager& networkManager, void (*resetWdt)()) :
  serialPort(serial),
  debugLed(debugLed),
  networkManager(networkManager),
  tcpClient(),
  mqttClient(tcpClient),
  networkState(true),
  mqttState(MQTT_CONNECTED),
  onlineState(true),
  deviceResetTimer(0U),
  resetWdt(resetWdt),
  common(*this, "common")
{}

bool Connectivity::init() {
  const uint32_t conTime = millis();
  debugLed.startTicker(500U);
  const uint8_t resetReason = ResetHandler::getResetReason();
  serialPort.printf_P(PSTR("%sInfo:\r\n"), INIT_PREFIX);
  serialPort.printf_P(PSTR("  CPP: %u\r\n"), Build::getCppVersion());
  serialPort.printf_P(PSTR("  FW: %hu\r\n"), Build::getFwVersion());
  serialPort.printf_P(PSTR("  GIT: %x\r\n"), Build::getGitHash());
  serialPort.printf_P(PSTR("  Dirty: %hu\r\n"), Build::getGitDirty());
  serialPort.printf_P(PSTR("  Reset reason: %hu\r\n"), resetReason);
  serialPort.flush();

  // Init filesystem.
  {
    delay(10U);
    uint32_t totalBytes = 0U, usedBytes = 0U, freeBytes = 0U;
    const bool initFS = ConfigHandler::initialiseFileSystem(totalBytes, usedBytes, freeBytes);
    serialPort.printf_P(PSTR("[FS] Initialising filesystem: %s\r\n"), Str::getStateStr(initFS));
    if(!initFS) { return false; }
    serialPort.printf_P(PSTR("  Total bytes: %u\r\n  Used bytes: %u\r\n  Free bytes: %u\r\n"), totalBytes, usedBytes, freeBytes);
  }

  // Start interface.
  resetWatchdogTimer();
  {
    const uint16_t connResult = networkManager.connect();
    const bool connResultOk = (connResult == 0U);
    serialPort.printf_P(PSTR("[NETWORK] Connection: %s\r\n"), Str::getStateStr(connResultOk));
    if(!connResultOk) {
      serialPort.printf_P(PSTR("  Code: %hu\r\n"), connResult);
      return false;
    }
  }

  // Set time via NTP, as required for x.509 validation.
  yield();
  resetWatchdogTimer();
  {
    serialPort.printf_P(PSTR("%sWaiting for NTP time sync"), NTP_PREFIX);
    configTime(0, 0, "0.hu.pool.ntp.org", "1.hu.pool.ntp.org", "2.hu.pool.ntp.org");
    time_t nowSecs = time(nullptr);
    while(nowSecs < 8 * 3600 * 2) {
      serialPort.print(".");
      delay(200U);
      nowSecs = time(nullptr);
    }
    serialPort.printf_P(PSTR("\r\n%sUTC ISO time: %s\r\n"), NTP_PREFIX, getISODateTime());
  }

  // Get MQTT server credentials.
  {
    const uint16_t credResult = ConfigHandler::getServerCredentials(mqttCredentials.userName, mqttCredentials.password, mqttCredentials.serverName, mqttCredentials.serverPort);
    const bool credResultOk = (credResult == 0U);
    serialPort.printf_P(PSTR("%sGetting server credentials: %s\r\n"), MQTT_PREFIX, Str::getStateStr(credResultOk));
    if(!credResultOk) {
      serialPort.printf_P(PSTR("  Code: %hu\r\n"), credResult);
      return false;
    }
  }

  // Setup MQTT topics.
  {
    const char* deviceID = strchr(Build::getPioEnv(), '_') + 1;
    const int32_t clientNameSize = snprintf_P(mqttCredentials.clientName, sizeof(mqttCredentials.clientName), "%s_%s", deviceID, networkManager.getMacAddressString());
    const int32_t senderTopicSize = snprintf_P(mqttCredentials.senderTopic, sizeof(mqttCredentials.senderTopic), "%s/%s/%s", BASE_TOPIC, SENDER_TOPIC, networkManager.getMacAddressString());
    const int32_t receiverTopicSize = snprintf_P(mqttCredentials.receiverTopic, sizeof(mqttCredentials.receiverTopic), "%s/%s/%s/#", BASE_TOPIC, RECEIVER_TOPIC, networkManager.getMacAddressString());
    const bool clientNameValid = (clientNameSize >= 0 && clientNameSize < static_cast<int32_t>(sizeof(mqttCredentials.clientName)));
    const bool senderTopicValid = (senderTopicSize >= 0 && senderTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.senderTopic)));
    const bool receiverTopicValid = (receiverTopicSize >= 0 && receiverTopicSize < static_cast<int32_t>(sizeof(mqttCredentials.receiverTopic)));
    serialPort.printf_P(PSTR("%sClient name: %s\r\n"), MQTT_PREFIX, Str::getStateStr(clientNameValid));
    serialPort.printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.clientName, clientNameSize);
    serialPort.printf_P(PSTR("%sSender topic: %s\r\n"), MQTT_PREFIX, Str::getStateStr(senderTopicValid));
    serialPort.printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.senderTopic, senderTopicSize);
    serialPort.printf_P(PSTR("%sReceiver topic: %s\r\n"), MQTT_PREFIX, Str::getStateStr(receiverTopicValid));
    serialPort.printf_P(PSTR("  %s Length: %d\r\n"), mqttCredentials.receiverTopic, receiverTopicSize);
    serialPort.flush();
    if(!clientNameValid) { return false; }
    if(!senderTopicValid) { return false; }
    if(!receiverTopicValid) { return false; }
  }

  // Open cert.
  {
    const uint8_t certResult = ConfigHandler::getServerCert([this](Stream& certFile, size_t certFileSize) -> bool {
#ifdef ESP8266
      serverCert.emplace(certFile, certFileSize);
      if(!serverCert.has_value()) { return false; }
      tcpClient.setTrustAnchors(&serverCert.value());
      tcpClient.setTimeout(Time::secToMs(5U));
      return true;
#elif defined ESP32
      tcpClient.setTimeout(10);
      return tcpClient.loadCACert(certFile, certFileSize);
#endif
    });
    const bool certResultOk = (certResult == 0U);
    serialPort.printf_P(PSTR("%sGetting server certificate: %s\r\n"), TCP_PREFIX, Str::getStateStr(certResultOk));
    if(!certResultOk) {
      serialPort.printf_P(PSTR("  Code: %hu\r\n"), certResult);
      return false;
    }
  }

  if(!connect()) { return false; }
  mqttClient.setCallback([this](const char* topic, uint8_t* payload, uint32_t length) { receiveMqttMessage(topic, payload, length); });

  {
    char versionString[80] = {'\0'};
    const int32_t versionStringSize = snprintf_P(versionString, sizeof(versionString), PSTR("{""\"CPP\":%u,\"FW\":%hu,\"GH\":\"%x\",\"Dirty\":%hu,\"RR\":%hu""}"),
      Build::getCppVersion(), Build::getFwVersion(), Build::getGitHash(), Build::getGitDirty(), resetReason);
    const bool versionStringValid = (versionStringSize >= 0 && versionStringSize < static_cast<int32_t>(sizeof(versionString)));
    if(!versionStringValid) { return false; }
    common.messageSend(versionString);
  }

  serialPort.printf_P(PSTR("%sInit registered objects:\r\n"), INIT_PREFIX);
  for(std::size_t i = 0; i < messageMap.size(); ++i) {
    const auto& currentObject = messageMap[i];
    if(currentObject != nullptr) {
      const bool beginResult = currentObject->begin();
      serialPort.printf_P(PSTR("  %zu. %s -> %s\r\n"), i, currentObject->getClassId(), Str::getStateStr(beginResult));
    }
    else {
      serialPort.printf_P(PSTR("  %zu. No object here!\r\n"), i);
    }
  }

  debugLed.stopTicker();
  serialPort.printf_P(PSTR("%sInit time was: %lums\r\n"), INIT_PREFIX, (millis() - conTime));
  return true;
}

bool Connectivity::connect() {
  yield();
  // TCP connection.
  const bool tcpConResult = tcpClient.connect(mqttCredentials.serverName, mqttCredentials.serverPort);
  serialPort.printf_P(PSTR("%sConnecting to: %s:%hu %s\r\n"), TCP_PREFIX, mqttCredentials.serverName, mqttCredentials.serverPort, Str::getStateStr(tcpConResult));
  if(!tcpConResult) { return false; }

  // MQTT connection.
  mqttClient.setServer(mqttCredentials.serverName, mqttCredentials.serverPort);
  const bool mqttConResult = mqttClient.connect(mqttCredentials.clientName, mqttCredentials.userName, mqttCredentials.password);
  serialPort.printf_P(PSTR("%sConnecting to MQTT broker: %s\r\n  State: %s\r\n"), MQTT_PREFIX, Str::getStateStr(mqttConResult), getMqttStatusStr(mqttClient.state()));
  if(!mqttConResult) { return false; }
  const bool subResult = mqttClient.subscribe(mqttCredentials.receiverTopic, 1);
  serialPort.printf_P(PSTR("%sSubscription: %s\r\n"), MQTT_PREFIX, Str::getStateStr(subResult));
  if(!subResult) { return false; }
  return true;
}

void Connectivity::run() {
  const uint32_t actualTime = millis();
  const bool actualNetworkState = networkManager.isNetworkAvailable();
  if(actualNetworkState != networkState) {
    networkState = actualNetworkState;
    if(networkState) {
      connect();
    } else {
      mqttClient.disconnect();
    }
  }
  const int8_t actualMqttState = mqttClient.state();
  if(mqttState != actualMqttState) {
    serialPort.printf_P(PSTR("%sStatus changed: %s -> %s\r\n"), MQTT_PREFIX, getMqttStatusStr(mqttState), getMqttStatusStr(actualMqttState));
    mqttState = actualMqttState;
  }

  if(!mqttClient.loop()) {
    if((mqttState < MQTT_CONNECTED) && networkState) {
      static uint32_t reconnectTimer = millis();
      if(millis() - reconnectTimer >= 10000U) {
        reconnectTimer = millis();
        connect();
      }
    }
  }

  for(const auto &currentObject : messageMap) {
    if(currentObject != nullptr) {
      currentObject->loop();
    }
  }

  const bool actualOnlineState = networkState && (mqttState == MQTT_CONNECTED);
  if(actualOnlineState) {
    deviceResetTimer = actualTime;
  }
  if(actualOnlineState != onlineState) {
    onlineState = actualOnlineState;
    onlineState ? debugLed.stopTicker() : debugLed.startTicker(250U);
    serialPort.printf_P(PSTR("%sDevice is: %s\r\n"), RUN_PREFIX, reinterpret_cast<const char*>(onlineState ? F("ONLINE") : F("OFFLINE")));
  }

  if(Time::hasElapsed(actualTime, deviceResetTimer, deviceResetTime)) {
    serialPort.printf_P(PSTR("%sDevice is offline since: %ums\r\n"), RUN_PREFIX, (actualTime - deviceResetTimer));
    ResetHandler::restartMCU();
  }
}

void Connectivity::receiveMqttMessage(const char* topic, uint8_t* payload, uint32_t length) {
  const char* classID = strrchr(topic, '/') + 1;
  if(!classID) { return; }
  for(const auto &currentObject : messageMap) {
    if(currentObject == nullptr) { return; }
    if(strcmp(currentObject->getClassId(), classID) == 0) {
      currentObject->messageReceived(payload, length);
      return;
    }
  }
  serialPort.printf_P(PSTR("%sNo handler -> %s\r\n"), MQTT_PREFIX, classID);
}

void Connectivity::sendMqttMessage(const char* subTopic, const char* payload) {
  char actualTopic[sizeof(mqttCredentials.senderTopic)];
  const int32_t actualTopicSize = snprintf_P(actualTopic, sizeof(actualTopic), "%s/%s", mqttCredentials.senderTopic, subTopic);
  const bool actualTopicValid = (actualTopicSize >= 0 && actualTopicSize < static_cast<int32_t>(sizeof(actualTopic)));
  if(!actualTopicValid) { return; }
  mqttClient.publish(actualTopic, payload);
}

const char* Connectivity::getISODateTime() {
  const time_t time_ = time(nullptr);
  static char buffer[24];
  memset(buffer, '\0', sizeof(buffer));
  struct tm * timeinfo;
  timeinfo = gmtime(&time_); // Convert time to UTC time structure
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", timeinfo); // Format as ISO UTC string
  return static_cast<const char*>(buffer);
}

bool Connectivity::registerCallback(Connectivity::MqttComBase* obj) {
  if(!obj) { return false; }
  messageMap.push_back(obj);
  return true;
}

const char* Connectivity::getMqttStatusStr(int8_t status) {
  switch(status) {
    case MQTT_CONNECTION_TIMEOUT: { return MQTT_CONNECTION_TIMEOUT_STR; } break;
    case MQTT_CONNECTION_LOST: { return MQTT_CONNECTION_LOST_STR; } break;
    case MQTT_CONNECT_FAILED: { return MQTT_CONNECT_FAILED_STR; } break;
    case MQTT_DISCONNECTED: { return MQTT_DISCONNECTED_STR; } break;
    case MQTT_CONNECTED: { return MQTT_CONNECTED_STR; } break;
    case MQTT_CONNECT_BAD_PROTOCOL: { return MQTT_CONNECT_BAD_PROTOCOL_STR; } break;
    case MQTT_CONNECT_BAD_CLIENT_ID: { return MQTT_CONNECT_BAD_CLIENT_ID_STR; } break;
    case MQTT_CONNECT_UNAVAILABLE: { return MQTT_CONNECT_UNAVAILABLE_STR; } break;
    case MQTT_CONNECT_BAD_CREDENTIALS: { return MQTT_CONNECT_BAD_CREDENTIALS_STR; } break;
    case MQTT_CONNECT_UNAUTHORIZED: { return MQTT_CONNECT_UNAUTHORIZED_STR; } break;
    default: { return MQTT_UNKNOWN_STATUS_STR; } break;
  }
}

//////////////////// -- MqttComBase class-- ////////////////////

Connectivity::MqttComBase::MqttComBase(Connectivity& connectivity, const char* classID) : conn(connectivity) {
  strlcpy(this->classId, classID, sizeof(this->classId));
  conn.registerCallback(this);
}

void Connectivity::MqttComBase::messageSend(const char* payload) const {
  conn.sendMqttMessage(getClassId(), payload);
}

const char* Connectivity::MqttComBase::getIsoTime() { return conn.getISODateTime(); }

bool Connectivity::MqttComBase::sendResponse(Response resp, uint16_t cmd) {
  static constexpr const uint8_t respBufSize = 28;
  char respBuf[respBufSize] = { '\0' };
  const int32_t respBufRealSize = snprintf_P(respBuf, sizeof(respBuf), PSTR("{""\"type\":%hu,""\"cmd\":%hu""}"), static_cast<uint8_t>(resp), cmd);
  const bool respBufValid = (respBufRealSize >= 0 && respBufRealSize < static_cast<int32_t>(sizeof(respBuf)));
  if(!respBufValid) { return false; }
  messageSend(respBuf);
  return true;
}

const char* Connectivity::MqttComBase::getClassId() const { return classId; }

//////////////////// -- Common class-- ////////////////////

const char Connectivity::Common::COMMON_PREFIX[] PROGMEM              = "[COMMON]";

Connectivity::Common::Common(Connectivity& connectivity, const char* classID) :
  MqttComBase(connectivity, classID),
  externalFileName{'\0'},
  dataTransfer(conn.serialPort)
{}

void Connectivity::Common::messageReceived(uint8_t* payload, uint32_t length) {
  JsonDocument cmdJson;
  DeserializationError deserializationError = deserializeJson(cmdJson, payload, length);
  const bool deSerResult = (deserializationError == DeserializationError::Code::Ok);
  if(!deSerResult) {
    conn.serialPort.printf_P(PSTR("%s Deserialisation failed: %s\r\n"), COMMON_PREFIX, reinterpret_cast<const char*>(deserializationError.f_str()));
    return;
  }
  const uint8_t cmd = cmdJson[F("cmd")].as<uint8_t>();
  Command command = static_cast<Command>(cmd);
  switch(command) {
    case Command::BLANK: {} break;
    case Command::RESTART: { ResetHandler::restartMCU(); } break;
    case Command::FW_DT_START:
    case Command::WIFICFG_DT_START:
    case Command::EXT_FILE_DT_START: {
      const uint32_t fileSize = cmdJson[F("fileSize")].as<uint32_t>();
      const uint32_t fileCrc = cmdJson[F("crc32")].as<uint32_t>();
      const char* fileNamePtr = nullptr;
      switch(command) {
        case Command::FW_DT_START: {
          const char* binId = cmdJson[F("binId")].as<const char*>();
          if(strncmp_P(binId, Build::getPioEnv(), Build::getPioEnvLength()) != 0) {
            conn.serialPort.printf_P(PSTR("%s Wrong FW file ID: %s\r\n"), COMMON_PREFIX, binId);
            MqttComBase::sendResponse(MqttComBase::Response::NACK, cmd);
            return;
          }
          fileNamePtr = FileName::getOtaFwLocation();
          } break;
        case Command::WIFICFG_DT_START: { fileNamePtr = FileName::getWifiTempConfigLocation(); } break;
        case Command::EXT_FILE_DT_START: {
          const char* fileName = cmdJson[F("name")].as<const char*>();
          memset(externalFileName, '\0', sizeof(externalFileName));
          if(fileName != nullptr) { memccpy(externalFileName, fileName, '\0', sizeof(externalFileName)); }
          uint32_t externalFileNameSize =  strnlen(externalFileName, sizeof(externalFileName));
          if(externalFileNameSize == 0 || externalFileNameSize >= sizeof(externalFileName)) {
            conn.serialPort.printf_P(PSTR("%s Wrong file name: missing / too long!\r\n"), COMMON_PREFIX);
            MqttComBase::sendResponse(MqttComBase::Response::NACK, cmd);
            return;
          }
          fileNamePtr = static_cast<const char*>(externalFileName);
        } break;
        default: {} break;
      }
      const bool transferBeginResult = dataTransfer.begin(fileSize, fileCrc, fileNamePtr);
      MqttComBase::sendResponse((transferBeginResult ? MqttComBase::Response::ACK : MqttComBase::Response::NACK), cmd);
      if(!transferBeginResult) {
        conn.serialPort.printf_P(PSTR("%s Can't begin file transfer:\r\n  Name: %s\r\n"), COMMON_PREFIX, fileNamePtr);
        return;
      }
    } break;
    case Command::FW_DT_DATA:
    case Command::WIFICFG_DT_DATA:
    case Command::EXT_FILE_DT_DATA: {
      const uint32_t filePieceNumber = cmdJson[F("piece")].as<uint32_t>();
      const char* filePieceB64 = cmdJson["data"].as<const char*>();
      const bool storingResult = dataTransfer.storeBase64(filePieceNumber, filePieceB64);
      MqttComBase::sendResponse(storingResult ? MqttComBase::Response::ACK : MqttComBase::Response::NACK, cmd);
      if(!storingResult) {
        conn.serialPort.printf_P(PSTR("%s File storing failed!\r\n"), COMMON_PREFIX);
      }
    } break;
    case Command::FW_DT_END:
    case Command::WIFICFG_DT_END:
    case Command::EXT_FILE_DT_END: {
      const bool validityCheckResult = dataTransfer.checkValidity();
      MqttComBase::sendResponse((validityCheckResult ? MqttComBase::Response::ACK : MqttComBase::Response::NACK), cmd);
      if(!validityCheckResult) {
        conn.serialPort.printf_P(PSTR("%s Stored file is not valid!\r\n"), COMMON_PREFIX);
        return;
      }
      if(command == Command::FW_DT_END) {
        const bool fwUpdatePreparationOk = dataTransfer.upgradeFirmware();
        if(!fwUpdatePreparationOk) {
          conn.serialPort.printf_P(PSTR("%s FW upgrade preparation failed!\r\n"), COMMON_PREFIX);
          return;
        }
        ResetHandler::restartMCU();
      }
    } break;
  };
}

bool Connectivity::Common::begin() { return true; }

bool Connectivity::Common::loop() { return true; }

void Connectivity::Common::messageSend(const char* payload) const {
  MqttComBase::messageSend(payload);
}