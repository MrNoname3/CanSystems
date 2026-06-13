#include "canMqttGateway.hpp"
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
    CanMqttGateway(canHandler, clientCanId, connectivity, subTopic, fwFileName)
  {}

  using CanMqttGateway::getCanAvailTopic;
  using CanMqttGateway::getCanInfoTopic;
  using CanMqttGateway::getCanSwVersion;
  using CanMqttGateway::getCanDeviceId;
  using CanMqttGateway::getCanDeviceName;

  static inline int      customMessages = 0;
  static inline int      customFrames   = 0;
  static inline uint16_t lastCustomCmd  = 0U;
  static void resetState() { customMessages = 0; customFrames = 0; lastCustomCmd = 0U; }

private:
  bool initLocal() override { return true; }
  bool runLocal() override { return true; }
  void processMessageArrived(JsonDocument& payloadJson) override { (void)payloadJson; ++customMessages; }
  void processCanFrameArrived(const CanHandler::CanFrame& canFrame) override {
    ++customFrames;
    lastCustomCmd = static_cast<uint16_t>(canFrame.cmd);
  }
};

static void resetEnv() {
  LittleFS.reset();
  MqttBase::resetState();
  CanHandler::resetState();
  TestGateway::resetState();
  setFakeMillis(0U);
}

// Injects a CAN frame as if it arrived from the client node (from = 26).
static void injectFrame(CanMqttGateway& gateway, uint16_t cmd, const uint8_t (&data)[8]) {
  CanBase& canSide = gateway;
  canSide.canFrameArrivedCallback(CanHandler::CanFrame(10U, cmd, 26U, data));
}

static void injectAck(CanMqttGateway& gateway, CanCmd cmd) {
  const uint8_t data[8] = {1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};   // data[0] = ACK
  injectFrame(gateway, static_cast<uint16_t>(cmd), data);
}

static bool runOnce(CanMqttGateway& gateway) {
  Task& task = gateway;
  return task.run();
}

static size_t countFrames(uint16_t cmd) {
  size_t count = 0U;
  for(const auto& frame : CanHandler::sentFrames) {
    if(static_cast<uint16_t>(frame.cmd) == cmd) { ++count; }
  }
  return count;
}

static const CanHandler::CanFrame* lastFrame(uint16_t cmd) {
  for(auto it = CanHandler::sentFrames.rbegin(); it != CanHandler::sentFrames.rend(); ++it) {
    if(static_cast<uint16_t>(it->cmd) == cmd) { return &(*it); }
  }
  return nullptr;
}

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
  CanHandler can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  IS_TRUE(std::string(gateway.getCanAvailTopic()) == "iot/dtos/aabbccddeeff/alert1/availability");
  IS_TRUE(std::string(gateway.getCanInfoTopic())  == "iot/dtos/aabbccddeeff/alert1/info");
  IS_TRUE(std::string(gateway.getCanDeviceId())   == "esp32_can_aabbccddeeff_alert1");
  IS_TRUE(std::string(gateway.getCanDeviceName()) == "ALERT1 ddeeff");  // MAC kept lowercase from the sender topic.
  IS_EQUAL(countFrames(static_cast<uint16_t>(CanCmd::FW_VERSION)), 1U);
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"offline"})"), 1U);
  END_IT
}

// ---- ping and online/offline tracking ----

bool test_ping_sent_after_ping_interval() {
  IT("run() sends a PING frame once the 1 s ping interval elapses");
  resetEnv();
  CanHandler can;
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

bool test_online_offline_transitions() {
  IT("a received frame marks the client online; 5 s of silence marks it offline");
  resetEnv();
  CanHandler can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());                              // retained offline
  IS_TRUE(runOnce(gateway));                          // boot: offline timer not yet elapsed -> online
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"online"})"), 1U);
  setFakeMillis(5001U);                               // 5 s of CAN silence
  IS_TRUE(runOnce(gateway));
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"offline"})"), 2U);
  const uint8_t pong[8] = {0U};
  injectFrame(gateway, static_cast<uint16_t>(CanCmd::PING), pong);
  IS_TRUE(runOnce(gateway));
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"online"})"), 2U);
  END_IT
}

// ---- incoming CAN frames ----

bool test_fw_version_frame_publishes_info() {
  IT("a FW_VERSION frame publishes the retained info payload and the sw version string");
  resetEnv();
  CanHandler can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  // fw = 0x0102 = 258, git = 0x0a0b0c0d, dirty = 1.
  const uint8_t version[8] = {0x02U, 0x01U, 0x0dU, 0x0cU, 0x0bU, 0x0aU, 1U, 0U};
  injectFrame(gateway, static_cast<uint16_t>(CanCmd::FW_VERSION), version);
  IS_TRUE(std::string(gateway.getCanSwVersion()) == "258 (0a0b0c0d)");
  bool infoFound = false;
  for(const auto& entry : MqttBase::retainedMessages) {
    if(entry.first == "alert1/info" && entry.second == R"({"fw":258,"git":"a0b0c0d","dirty":1,"rr":255})") {
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
  CanHandler can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());                               // 1 FW_VERSION + 1 offline so far
  const uint8_t empty[8] = {0U};
  injectFrame(gateway, static_cast<uint16_t>(CanCmd::RESTART), empty);
  IS_EQUAL(countFrames(static_cast<uint16_t>(CanCmd::FW_VERSION)), 2U);
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"offline"})"), 2U);
  IS_EQUAL(countRetained("alert1/availability", R"({"state":"online"})"), 1U);
  END_IT
}

bool test_button_event_frame_publishes_message() {
  IT("a BUTTON_EVENT frame publishes the button state on the button subtopic");
  resetEnv();
  CanHandler can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  const uint8_t button[8] = {3U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
  injectFrame(gateway, static_cast<uint16_t>(CanCmd::BUTTON_EVENT), button);
  IS_EQUAL(MqttBase::subtopicMessages.size(), 1U);
  IS_TRUE(MqttBase::subtopicMessages[0].first  == "alert1/button");
  IS_TRUE(MqttBase::subtopicMessages[0].second == R"({"Button":3})");
  END_IT
}

bool test_unknown_frame_goes_to_derived_handler() {
  IT("an unhandled CAN command is forwarded to processCanFrameArrived()");
  resetEnv();
  CanHandler can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  const uint8_t data[8] = {0U};
  injectFrame(gateway, static_cast<uint16_t>(CanCmd::READ_HUM_TEMP_LDR), data);
  IS_EQUAL(TestGateway::customFrames, 1);
  IS_EQUAL(TestGateway::lastCustomCmd, static_cast<uint16_t>(CanCmd::READ_HUM_TEMP_LDR));
  END_IT
}

// ---- incoming MQTT messages ----

bool test_generic_command_message_sends_can_frame() {
  IT("a {Command, Data} MQTT message is forwarded as a raw CAN frame");
  resetEnv();
  CanHandler can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  MqttBase& mqttSide = gateway;
  JsonDocument doc;
  IS_TRUE(deserializeJson(doc, R"({"Command":5,"Data":"1122334455667788"})") == DeserializationError::Ok);
  mqttSide.messageArrivedCallback(doc);
  const CanHandler::CanFrame* frame = lastFrame(5U);
  IS_TRUE(frame != nullptr);
  IS_EQUAL(frame->data[0], 0x88U);                    // little-endian memcpy of the hex value
  IS_EQUAL(frame->data[7], 0x11U);
  IS_EQUAL(TestGateway::customMessages, 0);
  END_IT
}

bool test_invalid_command_data_is_dropped() {
  IT("a {Command, Data} message with trailing garbage in Data sends nothing");
  resetEnv();
  CanHandler can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  MqttBase& mqttSide = gateway;
  JsonDocument doc;
  IS_TRUE(deserializeJson(doc, R"({"Command":5,"Data":"11ZZ"})") == DeserializationError::Ok);
  mqttSide.messageArrivedCallback(doc);
  IS_EQUAL(CanHandler::sentFrames.size(), 0U);
  IS_EQUAL(TestGateway::customMessages, 0);
  END_IT
}

bool test_other_message_goes_to_derived_handler() {
  IT("a non-command MQTT message is forwarded to processMessageArrived()");
  resetEnv();
  CanHandler can;
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
  CanHandler can;
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
  CanHandler can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  LittleFS.setFile(kFwFile, "ABCD");
  IS_TRUE(gateway.startOta(kFwFile));
  IS_TRUE(pumpUntilFrame(gateway, static_cast<uint16_t>(CanCmd::OTA_START), 1U));
  const uint8_t nack[8] = {0U};                       // data[0] = NACK
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
  CanHandler can;
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

bool test_ota_start_rejects_bad_input() {
  IT("startOta() rejects a null name, a relative path, and a missing file");
  resetEnv();
  CanHandler can;
  Connectivity conn;
  TestGateway gateway(can, 26U, conn, "alert1");
  Task& task = gateway;
  IS_TRUE(task.init());
  IS_FALSE(gateway.startOta(nullptr));
  IS_FALSE(gateway.startOta("relative.bin"));
  IS_FALSE(gateway.startOta("/missing.bin"));
  IS_FALSE(gateway.isOtaInProgress());
  END_IT
}

bool test_ota_rejects_empty_file() {
  IT("startOta() rejects an empty firmware file");
  resetEnv();
  CanHandler can;
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
bool test_ota_contract_gateway_to_device_storage() {
  IT("frames the gateway emits reconstruct on a real OTA storage object and validate");
  resetEnv();
  CanHandler can;
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

int main() {
  SUITE("CanMqttGateway");
  test_init_builds_topics_and_publishes_offline();
  test_ping_sent_after_ping_interval();
  test_online_offline_transitions();
  test_fw_version_frame_publishes_info();
  test_restart_frame_republishes_availability();
  test_button_event_frame_publishes_message();
  test_unknown_frame_goes_to_derived_handler();
  test_generic_command_message_sends_can_frame();
  test_invalid_command_data_is_dropped();
  test_other_message_goes_to_derived_handler();
  test_ota_happy_path();
  test_ota_nack_aborts_with_error_status();
  test_ota_timeout_reports_error();
  test_ota_start_rejects_bad_input();
  test_ota_rejects_empty_file();
  test_ota_contract_gateway_to_device_storage();
  FINISH
}
