#include "canMqttGateway.hpp"
#include "testCan.hpp"
#include "ota.hpp"
#include "Arduino.h"
#include "LittleFS.h"
#include "BDDTest.h"
#include "crc16.hpp"
#include <string>
#include <vector>

// Concrete gateway exposing the protected topic getters and counting the virtual hooks.
class TestGateway final : public CanMqttGateway {
public:
  TestGateway(CanHandler& canHandler, uint16_t clientCanId, Connectivity& connectivity,
              const char* subTopic, const char* fwFileName = nullptr) :
    CanMqttGateway(canHandler, clientCanId, connectivity, subTopic, fwFileName) {}

  using CanMqttGateway::getCanAvailTopic;
  using CanMqttGateway::getCanDeviceId;
  using CanMqttGateway::getCanDeviceName;
  using CanMqttGateway::getCanInfoTopic;
  using CanMqttGateway::getCanSwVersion;

  static inline int customMessages = 0;
  static inline int customFrames = 0;
  static inline uint16_t lastCustomCmd = 0U;
  static void resetState() {
    customMessages = 0;
    customFrames = 0;
    lastCustomCmd = 0U;
  }

private:
  bool initLocal() override { return true; }
  bool runLocal() override { return true; }
  void messageArrivedCallback(JsonVariant payloadJson) override {
    (void)payloadJson;
    ++customMessages;
  }
  void processCanFrameArrived(const CanHandler::CanFrame& canFrame) override {
    ++customFrames;
    lastCustomCmd = static_cast<uint16_t>(canFrame.cmd);
  }
};

static void resetEnv() {
  LittleFS.reset();
  MqttBase::resetState();
  esp32Can.reset();
  TestGateway::resetState();
  setFakeMillis(0U);
}

// Injects a CAN frame as if it arrived from the client node (from = 26).
static void injectFrame(CanMqttGateway& gateway, uint16_t cmd, const uint8_t (&data)[8]) {
  CanBase& canSide = gateway;
  canSide.canFrameArrivedCallback(CanHandler::CanFrame(10U, cmd, 26U, data));
}

static void injectAck(CanMqttGateway& gateway, CanCmd cmd) {
  const uint8_t data[8] = { 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U };   // data[0] = ACK
  injectFrame(gateway, static_cast<uint16_t>(cmd), data);
}

static bool runOnce(CanMqttGateway& gateway) {
  Task& task = gateway;
  return task.run();
}

static size_t countFrames(uint16_t cmd) { return countCanFrames(cmd); }

static const CanHandler::CanFrame* lastFrame(uint16_t cmd) { return lastCanFrame(cmd); }

static size_t countRetained(const char* subSubTopic, const char* payload) {
  size_t count = 0U;
  for(const auto& entry : MqttBase::retainedMessages) {
    if(entry.first == subSubTopic && entry.second == payload) { ++count; }
  }
  return count;
}

// ---- init and topic building ----

bool test_init_builds_topics_and_publishes_offline() {
  IT("init() builds the CAN topics, sends FW_VERSION and publishes retained offline");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  IS_TRUE(std::string(gateway.getCanAvailTopic()) == "iot/dtos/aabbccddeeff/alert1/availability");
  IS_TRUE(std::string(gateway.getCanInfoTopic()) == "iot/dtos/aabbccddeeff/alert1/info");
  IS_TRUE(std::string(gateway.getCanDeviceId()) == "esp32_can_aabbccddeeff_alert1");
  IS_TRUE(std::string(gateway.getCanDeviceName()) == "ALERT1 ddeeff");  // MAC kept lowercase from the sender topic.
  IS_EQUAL(countFrames(static_cast<uint16_t>(CanCmd::FW_VERSION)), 1U);
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"offline"})"), 1U);
  END_IT
}

// ---- ping and online/offline tracking ----

bool test_ping_sent_after_ping_interval() {
  IT("run() sends a PING frame once the 1 s ping interval elapses");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  IS_TRUE(runOnce(gateway));
  IS_EQUAL(countFrames(static_cast<uint16_t>(CanCmd::PING)), 0U);
  setFakeMillis(1001U);
  IS_TRUE(runOnce(gateway));
  IS_EQUAL(countFrames(static_cast<uint16_t>(CanCmd::PING)), 1U);
  END_IT
}

bool test_a_silent_client_is_never_announced_online() {
  IT("a client that has never answered is not announced online, whatever millis() reads at init()");
  resetEnv();
  setFakeMillis(3000U);                               // the gateway reached this driver 3 s after boot
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  IS_TRUE(runOnce(gateway));
  setFakeMillis(4900U);
  IS_TRUE(runOnce(gateway));
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"online"})"), 0U);
  END_IT
}

bool test_online_offline_transitions() {
  IT("a received frame marks the client online; 5 s of silence marks it offline");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());                              // retained offline
  const uint8_t pong[8] = { 0U };
  injectFrame(gateway, static_cast<uint16_t>(CanCmd::PING), pong);   // the client answers
  IS_TRUE(runOnce(gateway));
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"online"})"), 1U);
  setFakeMillis(5001U);                               // 5 s of CAN silence
  IS_TRUE(runOnce(gateway));
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"offline"})"), 2U);
  injectFrame(gateway, static_cast<uint16_t>(CanCmd::PING), pong);
  IS_TRUE(runOnce(gateway));
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"online"})"), 2U);
  END_IT
}

// ---- incoming CAN frames ----

bool test_fw_version_frame_publishes_info() {
  IT("a FW_VERSION frame publishes the retained info payload and the sw version string");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  // fw = 0x0102 = 258, git = 0x0a0b0c0d, dirty = 1, reset reason = 0x18 (WDRF + intentional).
  const uint8_t version[8] = { 0x02U, 0x01U, 0x0dU, 0x0cU, 0x0bU, 0x0aU, 1U, 0x18U };
  injectFrame(gateway, static_cast<uint16_t>(CanCmd::FW_VERSION), version);
  IS_TRUE(std::string(gateway.getCanSwVersion()) == "258 (0a0b0c0d)");
  bool infoFound = false;
  for(const auto& entry : MqttBase::retainedMessages) {
    if(entry.first == "alert1/info" && entry.second == R"({"fw":258,"git":"0a0b0c0d","dirty":1,"rr":24,"boot":0})") {
      infoFound = true;
    }
  }
  IS_TRUE(infoFound);
  // The frame also brings the client online immediately.
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"online"})"), 1U);
  END_IT
}

bool test_restart_frame_republishes_availability() {
  IT("a RESTART frame requests FW_VERSION and pulses offline+online availability");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());                               // 1 FW_VERSION + 1 offline so far
  const uint8_t empty[8] = { 0U };
  injectFrame(gateway, static_cast<uint16_t>(CanCmd::RESTART), empty);
  IS_EQUAL(countFrames(static_cast<uint16_t>(CanCmd::FW_VERSION)), 2U);
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"offline"})"), 2U);
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"online"})"), 1U);
  END_IT
}

bool test_button_event_frame_publishes_message() {
  IT("a BUTTON_EVENT frame publishes the button state on the button subtopic");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  const uint8_t button[8] = { 3U, 0U, 0U, 0U, 0U, 0U, 0U, 0U };
  injectFrame(gateway, static_cast<uint16_t>(CanCmd::BUTTON_EVENT), button);
  IS_EQUAL(MqttBase::subtopicMessages.size(), 1U);
  IS_TRUE(MqttBase::subtopicMessages[0].first == "alert1/button");
  IS_TRUE(MqttBase::subtopicMessages[0].second == R"({"Button":3})");
  END_IT
}

bool test_unknown_frame_goes_to_derived_handler() {
  IT("an unhandled CAN command is forwarded to processCanFrameArrived()");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  const uint8_t data[8] = { 0U };
  injectFrame(gateway, static_cast<uint16_t>(AlertCmd::READ_HUM_TEMP_LDR), data);
  IS_EQUAL(TestGateway::customFrames, 1);
  IS_EQUAL(TestGateway::lastCustomCmd, static_cast<uint16_t>(AlertCmd::READ_HUM_TEMP_LDR));
  END_IT
}

// ---- incoming MQTT messages ----

bool test_other_message_goes_to_derived_handler() {
  IT("an MQTT message reaches the driver's own handler");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  MqttBase& mqttSide = gateway;
  JsonDocument doc;
  IS_TRUE(deserializeJson(doc, R"({"Sound":3,"Volume":20})") == DeserializationError::Ok);
  mqttSide.messageArrivedCallback(doc);
  IS_EQUAL(TestGateway::customMessages, 1);
  END_IT
}

// ---- CAN OTA ----

static constexpr const char* kFwFile = "/canAlertFw.bin";

// Pumps run() until the OTA state machine emits the next frame of the given command.
static bool pumpUntilFrame(CanMqttGateway& gateway, uint16_t cmd, size_t expectedCount, int maxRuns = 8) {
  for(int i = 0; i < maxRuns; ++i) {
    (void)runOnce(gateway);
    if(countFrames(cmd) >= expectedCount) { return true; }
  }
  return false;
}

bool test_ota_happy_path() {
  IT(R"(a full CAN OTA streams the file in 4-byte pieces and reports {"OTA":"[OK]"})");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  const std::string content = "ABCDEFGH";             // 8 bytes -> 2 pieces
  LittleFS.setFile(kFwFile, content);
  IS_TRUE(gateway.startOta(kFwFile));
  IS_TRUE(gateway.isOtaInProgress());

  // CRC pass + OTA_START frame.
  IS_TRUE(pumpUntilFrame(gateway, static_cast<uint16_t>(CanCmd::OTA_START), 1U));
  const CanHandler::CanFrame* start = lastFrame(static_cast<uint16_t>(CanCmd::OTA_START));
  IS_TRUE(start != nullptr);
  IS_EQUAL(start->data[0], 0U);                       // storage number 0
  IS_EQUAL(start->data[1], 0U);
  IS_EQUAL(start->data[2], 8U);                       // file size 8
  IS_EQUAL(start->data[3], 0U);
  const uint16_t expectedCrc = Crc16::calculate(reinterpret_cast<const uint8_t*>(content.data()),
                                                static_cast<uint32_t>(content.size()));
  IS_EQUAL(start->data[6], static_cast<uint8_t>(expectedCrc & 0xFFU));
  IS_EQUAL(start->data[7], static_cast<uint8_t>((expectedCrc >> 8U) & 0xFFU));

  // First piece after the START ack.
  injectAck(gateway, CanCmd::OTA_START);
  IS_TRUE(pumpUntilFrame(gateway, static_cast<uint16_t>(CanCmd::OTA_SEND), 1U));
  const CanHandler::CanFrame* piece0 = lastFrame(static_cast<uint16_t>(CanCmd::OTA_SEND));
  IS_TRUE(piece0 != nullptr);
  IS_EQUAL(piece0->data[0], static_cast<uint8_t>('A'));
  IS_EQUAL(piece0->data[3], static_cast<uint8_t>('D'));
  IS_EQUAL(piece0->data[4], 0U);                      // frame number 0

  // Second piece after the first ack.
  injectAck(gateway, CanCmd::OTA_SEND);
  IS_TRUE(pumpUntilFrame(gateway, static_cast<uint16_t>(CanCmd::OTA_SEND), 2U));
  const CanHandler::CanFrame* piece1 = lastFrame(static_cast<uint16_t>(CanCmd::OTA_SEND));
  IS_TRUE(piece1 != nullptr);
  IS_EQUAL(piece1->data[0], static_cast<uint8_t>('E'));
  IS_EQUAL(piece1->data[4], 4U);                      // frame number 4

  // Client validates and confirms with OTA_END ACK.
  injectAck(gateway, CanCmd::OTA_SEND);
  IS_TRUE(runOnce(gateway));                          // STORE with 0 remaining -> WAIT_FOR_ACK
  injectAck(gateway, CanCmd::OTA_END);
  IS_TRUE(runOnce(gateway));                          // VALID -> status + cleanup
  IS_FALSE(gateway.isOtaInProgress());
  bool statusOk = false;
  for(const auto& entry : MqttBase::subtopicMessages) {
    if(entry.first == "alert1/ota" && entry.second == R"({"OTA":"[OK]"})") { statusOk = true; }
  }
  IS_TRUE(statusOk);
  END_IT
}

bool test_ota_nack_aborts_with_error_status() {
  IT(R"(a NACK during the OTA aborts it and reports {"OTA":"[ERR]"})");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  LittleFS.setFile(kFwFile, "ABCD");
  IS_TRUE(gateway.startOta(kFwFile));
  IS_TRUE(pumpUntilFrame(gateway, static_cast<uint16_t>(CanCmd::OTA_START), 1U));
  const uint8_t nack[8] = { 0U };                       // data[0] = NACK
  injectFrame(gateway, static_cast<uint16_t>(CanCmd::OTA_START), nack);
  IS_TRUE(runOnce(gateway));                          // INVALID -> status + cleanup
  IS_FALSE(gateway.isOtaInProgress());
  bool statusErr = false;
  for(const auto& entry : MqttBase::subtopicMessages) {
    if(entry.first == "alert1/ota" && entry.second == R"({"OTA":"[ERR]"})") { statusErr = true; }
  }
  IS_TRUE(statusErr);
  END_IT
}

bool test_ota_timeout_reports_error() {
  IT(R"(an OTA stuck waiting for an ACK times out into {"OTA":"[ERR]"} and cleans up)");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  LittleFS.setFile(kFwFile, "ABCD");
  IS_TRUE(gateway.startOta(kFwFile));
  IS_TRUE(pumpUntilFrame(gateway, static_cast<uint16_t>(CanCmd::OTA_START), 1U));
  IS_TRUE(gateway.isOtaInProgress());                 // WAIT_FOR_ACK, no answer comes
  setFakeMillis(5U * 60U * 1000U + 1U);               // > 5 minute OTA timeout
  IS_TRUE(runOnce(gateway));                          // timeout -> INVALID -> status + cleanup
  IS_FALSE(gateway.isOtaInProgress());
  bool statusErr = false;
  for(const auto& entry : MqttBase::subtopicMessages) {
    if(entry.first == "alert1/ota" && entry.second == R"({"OTA":"[ERR]"})") { statusErr = true; }
  }
  IS_TRUE(statusErr);
  END_IT
}

bool test_stray_ota_ack_while_idle_is_ignored() {
  IT("an OTA ACK arriving with no transfer running is ignored");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  IS_FALSE(gateway.isOtaInProgress());
  injectAck(gateway, CanCmd::OTA_SEND);               // late or duplicated ACK from an earlier transfer
  IS_FALSE(gateway.isOtaInProgress());                // no phantom transfer starts
  IS_TRUE(runOnce(gateway));
  setFakeMillis(5U * 60U * 1000U + 1U);               // past the 5 minute OTA timeout
  IS_TRUE(runOnce(gateway));
  for(const auto& entry : MqttBase::subtopicMessages) {
    IS_FALSE(entry.first == "alert1/ota");            // no status for a transfer that never ran
  }
  END_IT
}

bool test_second_ota_start_is_rejected_while_one_runs() {
  IT("starting a second OTA while one is in progress is rejected and leaves it running");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  LittleFS.setFile(kFwFile, "ABCD");
  IS_TRUE(gateway.startOta(kFwFile));
  IS_TRUE(pumpUntilFrame(gateway, static_cast<uint16_t>(CanCmd::OTA_START), 1U));
  IS_TRUE(gateway.isOtaInProgress());
  const size_t startFrames = countFrames(static_cast<uint16_t>(CanCmd::OTA_START));

  IS_FALSE(gateway.startOta(kFwFile));                // rejected, not silently restarted
  IS_TRUE(gateway.isOtaInProgress());
  IS_EQUAL(countFrames(static_cast<uint16_t>(CanCmd::OTA_START)), startFrames);

  injectAck(gateway, CanCmd::OTA_START);              // the running transfer still proceeds
  IS_TRUE(pumpUntilFrame(gateway, static_cast<uint16_t>(CanCmd::OTA_SEND), 1U));
  END_IT
}

bool test_ota_start_rejects_bad_input() {
  IT("startOta() rejects a null name and a relative path without starting anything");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  IS_FALSE(gateway.startOta(nullptr));
  IS_FALSE(gateway.startOta("relative.bin"));
  IS_FALSE(gateway.isOtaInProgress());                     // nothing was opened, nothing to clean up
  END_IT
}

bool test_ota_start_reports_a_missing_file_to_the_server() {
  IT("a firmware file that cannot be opened is reported over MQTT, as an empty one is");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  IS_FALSE(gateway.startOta("/missing.bin"));
  MqttBase::subtopicMessages.clear();
  (void)runOnce(gateway);                                  // the INVALID pass publishes and cleans up
  IS_EQUAL(MqttBase::subtopicMessages.size(), 1U);
  IS_TRUE(MqttBase::subtopicMessages[0].first == "alert1/ota");
  IS_TRUE(MqttBase::subtopicMessages[0].second == R"({"OTA":"[ERR]"})");
  IS_FALSE(gateway.isOtaInProgress());
  END_IT
}

bool test_ota_rejects_empty_file() {
  IT("startOta() rejects an empty firmware file");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  LittleFS.setFile(kFwFile, "");
  IS_FALSE(gateway.startOta(kFwFile));
  END_IT
}

// Crosses the boundary the gateway and the device storage are otherwise only tested against in
// isolation: the frames the gateway actually emits are unpacked (via the shared OtaCanFrame, exactly
// as canHandlerAtmega328P does) into a real OTA storage object. It validates only if the gateway's
// CRC matches the device's recomputed CRC, the byte offsets line up, and the partial last piece
// agrees -- i.e. if the two hand-maintained sides of the wire format still agree.
bool test_a_known_checksum_skips_the_read_pass() {
  IT("an image whose checksum a previous target computed is sent without reading the file again");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway first(can, 26U, conn, "alert1");
  Task& firstTask = first;
  IS_TRUE(firstTask.init());
  const std::string content(200U, 'Z');            // 200 bytes: four 64-byte checksum passes
  LittleFS.setFile(kFwFile, content);

  OtaImageInfo image{};
  IS_TRUE(first.startOta(kFwFile, image));
  IS_FALSE(image.valid);                           // nothing known about it yet
  (void)runOnce(first);
  IS_EQUAL(countFrames(static_cast<uint16_t>(CanCmd::OTA_START)), 0U);  // still checksumming
  IS_TRUE(pumpUntilFrame(first, static_cast<uint16_t>(CanCmd::OTA_START), 1U));
  const CanHandler::CanFrame* startFrame = lastFrame(static_cast<uint16_t>(CanCmd::OTA_START));
  IS_TRUE(startFrame != nullptr);
  const OtaCanFrame::StartFrame parsed = OtaCanFrame::unpackStart(startFrame->data);
  IS_TRUE(image.valid);                            // the pass left its result behind
  IS_EQUAL(image.size, 200U);
  IS_EQUAL(image.crc, parsed.fwCrc);

  // The next target of the same upload gets the checksum handed to it.
  esp32Can.clearTransmitted();
  TestGateway second(can, 27U, conn, "alert2");
  Task& secondTask = second;
  IS_TRUE(secondTask.init());
  IS_TRUE(second.startOta(kFwFile, image));
  (void)runOnce(second);                           // one pass is enough now
  const CanHandler::CanFrame* secondStart = lastFrame(static_cast<uint16_t>(CanCmd::OTA_START));
  IS_TRUE(secondStart != nullptr);
  const OtaCanFrame::StartFrame secondParsed = OtaCanFrame::unpackStart(secondStart->data);
  IS_EQUAL(secondParsed.fwCrc, parsed.fwCrc);
  IS_EQUAL(secondParsed.fwSize, 200U);
  END_IT
}

bool test_ota_contract_gateway_to_device_storage() {
  IT("frames the gateway emits reconstruct on a real OTA storage object and validate");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  const std::string content = "OTA-CONTRACT!!";       // 14 bytes -> 4 pieces, last one partial (2 bytes)
  LittleFS.setFile(kFwFile, content);

  SPIFlash flash(0U);
  OTA ota(flash);

  IS_TRUE(gateway.startOta(kFwFile));

  // OTA_START: feed the gateway's own start frame into the device storage.
  IS_TRUE(pumpUntilFrame(gateway, static_cast<uint16_t>(CanCmd::OTA_START), 1U));
  const CanHandler::CanFrame* startFrame = lastFrame(static_cast<uint16_t>(CanCmd::OTA_START));
  IS_TRUE(startFrame != nullptr);
  const OtaCanFrame::StartFrame parsedStart = OtaCanFrame::unpackStart(startFrame->data);
  IS_TRUE(ota.start(parsedStart.storageNumber, parsedStart.fwSize, parsedStart.fwCrc));
  injectAck(gateway, CanCmd::OTA_START);

  // OTA_SEND: stream every emitted piece into storage, ACKing each as the real device would.
  for(int guard = 0; guard < 64; guard++) {
    const size_t before = countFrames(static_cast<uint16_t>(CanCmd::OTA_SEND));
    (void)runOnce(gateway);
    if(countFrames(static_cast<uint16_t>(CanCmd::OTA_SEND)) == before) { break; }  // no new piece -> all sent
    const CanHandler::CanFrame* piece = lastFrame(static_cast<uint16_t>(CanCmd::OTA_SEND));
    IS_TRUE(piece != nullptr);
    const OtaCanFrame::SendFrame parsedSend = OtaCanFrame::unpackSend(piece->data);
    IS_TRUE(ota.storeNextData(parsedSend.dataAddress, parsedSend.data));
    injectAck(gateway, CanCmd::OTA_SEND);
  }

  // The storage validates against the CRC the gateway computed: the cross-side agreement check.
  OTA::OtaState deviceState = OTA::OtaState::IDLE;
  for(int i = 0; i < 256; i++) {
    deviceState = ota.run();
    if(deviceState == OTA::OtaState::VALID || deviceState == OTA::OtaState::INVALID) { break; }
  }
  IS_EQUAL(deviceState, OTA::OtaState::VALID);
  for(size_t i = 0U; i < content.size(); i++) {
    IS_EQUAL(flash.readByte(static_cast<uint32_t>(i)), static_cast<uint8_t>(content[i]));
  }
  END_IT
}

// ---- address assignment ----

bool test_an_address_request_reaches_the_bus() {
  IT("requestCanIdChange puts a SET_CAN_ID frame naming both addresses on the bus");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());

  IS_TRUE(gateway.requestCanIdChange(28U));
  const CanHandler::CanFrame* frame = lastCanFrame(static_cast<uint16_t>(CanCmd::SET_CAN_ID));
  IS_TRUE(frame != nullptr);
  if(frame != nullptr) {
    IS_EQUAL(static_cast<uint16_t>(frame->to), 26U);   // still addressed on the id it holds now
    const CanIdAssign::Request request = CanIdAssign::unpack(frame->data);
    IS_EQUAL(request.expectedLocal, 26U);
    IS_EQUAL(request.newLocal, 28U);
    IS_TRUE(request.reservedClear);
  }
  END_IT
}

bool test_the_address_it_already_holds_is_sent() {
  IT("being told to keep the address it already holds is passed on, not refused as a duplicate");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());

  // The device is registered on 26 itself, so the duplicate check has to look past it - a master
  // that lost an answer repeats the request, and the node accepts it.
  IS_TRUE(gateway.requestCanIdChange(26U));
  const CanHandler::CanFrame* frame = lastCanFrame(static_cast<uint16_t>(CanCmd::SET_CAN_ID));
  IS_TRUE(frame != nullptr);
  if(frame != nullptr) {
    const CanIdAssign::Request request = CanIdAssign::unpack(frame->data);
    IS_EQUAL(request.expectedLocal, 26U);
    IS_EQUAL(request.newLocal, 26U);
  }
  END_IT
}

bool test_the_nodes_answer_is_consumed_here() {
  IT("the node's answer to SET_CAN_ID is handled by the gateway, not passed to the derived class");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  TestGateway::resetState();

  const uint8_t ack[8] = { static_cast<uint8_t>(CanHandler::Response::ACK), 0U, 0U, 0U, 0U, 0U, 0U, 0U };
  CanBase& canSide = gateway;
  canSide.canFrameArrivedCallback(CanHandler::CanFrame(10U, static_cast<uint16_t>(CanCmd::SET_CAN_ID), 26U, ack));
  IS_EQUAL(TestGateway::customFrames, 0);
  END_IT
}

bool test_an_address_another_device_holds_is_refused() {
  IT("an address one of the handler's own devices answers on is refused");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  const TestGateway neighbour(can, 27U, conn, "alert2");
  Task& task = gateway;
  IS_TRUE(task.init());

  // Two of them on one address would talk over each other on the bus.
  IS_FALSE(gateway.requestCanIdChange(27U));
  IS_EQUAL(countCanFrames(static_cast<uint16_t>(CanCmd::SET_CAN_ID)), 0U);
  END_IT
}

bool test_an_unusable_address_is_refused() {
  IT("zero and an address past the 10-bit field never reach the bus");
  resetEnv();
  TestCan can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());

  IS_FALSE(gateway.requestCanIdChange(0U));
  IS_FALSE(gateway.requestCanIdChange(CanIdAssign::idMask + 1U));
  IS_EQUAL(countCanFrames(static_cast<uint16_t>(CanCmd::SET_CAN_ID)), 0U);
  END_IT
}

int main() {
  SUITE("CanMqttGateway");
  test_init_builds_topics_and_publishes_offline();
  test_ping_sent_after_ping_interval();
  test_a_silent_client_is_never_announced_online();
  test_online_offline_transitions();
  test_fw_version_frame_publishes_info();
  test_restart_frame_republishes_availability();
  test_button_event_frame_publishes_message();
  test_unknown_frame_goes_to_derived_handler();
  test_other_message_goes_to_derived_handler();
  test_ota_happy_path();
  test_ota_nack_aborts_with_error_status();
  test_ota_timeout_reports_error();
  test_stray_ota_ack_while_idle_is_ignored();
  test_second_ota_start_is_rejected_while_one_runs();
  test_ota_start_rejects_bad_input();
  test_ota_start_reports_a_missing_file_to_the_server();
  test_ota_rejects_empty_file();
  test_a_known_checksum_skips_the_read_pass();
  test_ota_contract_gateway_to_device_storage();
  test_an_address_request_reaches_the_bus();
  test_the_address_it_already_holds_is_sent();
  test_the_nodes_answer_is_consumed_here();
  test_an_address_another_device_holds_is_refused();
  test_an_unusable_address_is_refused();
  FINISH
}
