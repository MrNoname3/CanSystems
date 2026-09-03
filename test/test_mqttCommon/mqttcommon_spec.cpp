#include "mqttCommon.hpp"
#include "Arduino.h"
#include "LittleFS.h"
#include "Update.h"
#include "resetHandler.hpp"
#include "otaRegistry.hpp"
#include "BDDTest.h"
#include <ArduinoJson.h>

static void resetEnv() {
  LittleFS.reset();
  Update.reset();
  MqttBase::resetState();
  ResetHandler::resetState();
}

static void deliver(MqttCommon& mc, const char* json) {
  JsonDocument doc;
  (void)deserializeJson(doc, json);
  mc.messageArrivedCallback(doc);
}

// ---- command dispatch ----

bool test_cmd_reboot() {
  IT("a reboot command ACKs, shuts down MQTT and restarts the MCU");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  deliver(mc, R"({"cmd":"reboot"})");
  IS_EQUAL(MqttBase::responseCount, 1);
  IS_TRUE(MqttBase::lastResponse == MqttBase::Response::ACK);
  IS_EQUAL(MqttBase::shutdownCount, 1);
  IS_EQUAL(ResetHandler::restartCount, 1);
  END_IT
}

bool test_cmd_unknown() {
  IT("an unknown command is NACKed and does not restart");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  deliver(mc, R"({"cmd":"nope"})");
  IS_EQUAL(MqttBase::responseCount, 1);
  IS_TRUE(MqttBase::lastResponse == MqttBase::Response::NACK);
  IS_EQUAL(ResetHandler::restartCount, 0);
  END_IT
}

// ---- file-begin routing ----

bool test_file_begin_valid() {
  IT("a valid file-begin message starts the transfer and ACKs with no error");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  deliver(mc, R"({"name":"/canAlertFw.bin","fileSize":3,"md5":"900150983cd24fb0d6963f7d28e17f72"})");
  IS_TRUE(MqttBase::lastResponse == MqttBase::Response::ACK);
  IS_EQUAL(MqttBase::lastErrCode, 0U);
  END_IT
}

bool test_file_begin_disallowed_name() {
  IT("a file-begin for a disallowed name is NACKed with FILE_NAME_NOT_ALLOWED");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  deliver(mc, R"({"name":"/etc/shadow","fileSize":3,"md5":"900150983cd24fb0d6963f7d28e17f72"})");
  IS_TRUE(MqttBase::lastResponse == MqttBase::Response::NACK);
  IS_EQUAL(MqttBase::lastErrCode, 1UL << 5U);
  END_IT
}

bool test_fw_begin_wrong_binid() {
  IT("a firmware message with a mismatched binId is rejected before the transfer starts");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  deliver(mc, R"({"binId":"someotherdev","name":"espFirmware","fileSize":3,"md5":"900150983cd24fb0d6963f7d28e17f72"})");
  IS_TRUE(MqttBase::lastResponse == MqttBase::Response::NACK);
  END_IT
}

bool test_fw_begin_correct_binid() {
  IT("a firmware message whose binId matches this device begins the OTA and ACKs");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  // Build::getPioEnv() == "native_test" in this build.
  deliver(mc, R"({"binId":"native_test","name":"espFirmware","fileSize":3,"md5":"900150983cd24fb0d6963f7d28e17f72"})");
  IS_TRUE(MqttBase::lastResponse == MqttBase::Response::ACK);
  IS_EQUAL(MqttBase::lastErrCode, 0U);
  END_IT
}

bool test_file_piece_stored() {
  IT("a file-piece message after begin stores the piece and ACKs");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  deliver(mc, R"({"name":"/canAlertFw.bin","fileSize":3,"md5":"900150983cd24fb0d6963f7d28e17f72"})");
  deliver(mc, R"({"piece":0,"data":"YWJj"})");
  IS_TRUE(MqttBase::lastResponse == MqttBase::Response::ACK);
  END_IT
}

bool test_unknown_json_no_response() {
  IT("a JSON message with no command or file fields produces no response");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  deliver(mc, R"({"foo":1,"bar":"x"})");
  IS_EQUAL(MqttBase::responseCount, 0);
  END_IT
}

// ---- end-to-end: receive -> verify (real MD5) -> route to OTA target ----

class TestOtaTarget final : public OtaTarget {
public:
  explicit TestOtaTarget(const char* name) :
    name_(name) {}
  [[nodiscard]] const char* getFwFileName() const override { return name_; }
  [[nodiscard]] bool isOtaTargetOnline() const override { return true; }
  [[nodiscard]] bool isOtaInProgress() const override { return false; }
  /// @brief One turn of the target's own run(), where a queued transfer is picked up.
  void poll() {
    OtaImageInfo image{};
    if(OtaRegistry::claimStart(*this, image)) { triggered = true; }
  }
  bool triggered = false;

private:
  const char* name_;
};

bool test_end_to_end_file_routes_to_ota_target() {
  IT("a completed file transfer verifies the real MD5 and routes to the matching OTA target");
  resetEnv();
  static TestOtaTarget target("/canAlertFw.bin");
  target.triggered = false;
  OtaRegistry::add(target);  // idempotent

  Connectivity conn;
  MqttCommon mc(conn, "common");
  deliver(mc, R"({"name":"/canAlertFw.bin","fileSize":3,"md5":"900150983cd24fb0d6963f7d28e17f72"})");  // begin
  deliver(mc, R"({"piece":0,"data":"YWJj"})");             // store base64("abc") -> completes -> CHECK
  (void)mc.run();                                          // CHECK: read + hash
  (void)mc.run();                                          // CHECK: real MD5 matches -> rename + mark valid
  (void)mc.run();                                          // consume completion -> ACK + queue the OTA target
  target.poll();                                           // the target starts from its own run()

  IS_TRUE(target.triggered);
  IS_TRUE(MqttBase::lastResponse == MqttBase::Response::ACK);
  IS_TRUE(LittleFS.exists("/canAlertFw.bin"));
  END_IT
}

bool test_firmware_without_a_binid_is_rebooted_into() {
  IT("firmware that arrives without a binId is rebooted into, not left staged");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  deliver(mc, R"({"name":"espFirmware","fileSize":3,"md5":"900150983cd24fb0d6963f7d28e17f72"})");  // begin
  deliver(mc, R"({"piece":0,"data":"YWJj"})");             // base64("abc") -> Update.end() verifies it
  (void)mc.run();                                          // consume completion
  IS_EQUAL(ResetHandler::restartCount, 1);
  END_IT
}

bool test_a_config_file_carrying_a_binid_is_not_rebooted_into() {
  IT("a file that is not firmware does not restart the device, whatever binId the message carries");
  resetEnv();
  static TestOtaTarget target("/canAlertFw.bin");
  target.triggered = false;
  OtaRegistry::add(target);  // idempotent

  Connectivity conn;
  MqttCommon mc(conn, "common");
  deliver(mc, R"({"binId":"native_test","name":"/canAlertFw.bin","fileSize":3,"md5":"900150983cd24fb0d6963f7d28e17f72"})");
  deliver(mc, R"({"piece":0,"data":"YWJj"})");
  (void)mc.run();                                          // CHECK: read + hash
  (void)mc.run();                                          // CHECK: MD5 matches -> rename
  (void)mc.run();                                          // consume completion
  target.poll();
  IS_EQUAL(ResetHandler::restartCount, 0);
  IS_TRUE(target.triggered);
  END_IT
}

// ---- lifecycle / discovery / failure logging ----

bool test_init_returns_true() {
  IT("init() returns true");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  IS_TRUE(mc.init());
  END_IT
}

bool test_publish_discovery_returns_true() {
  IT("publishDiscovery() builds the reboot-button entity and returns true");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  IS_TRUE(mc.publishDiscovery());
  END_IT
}

bool test_response_send_failure_is_handled() {
  IT("a failed response send is handled (still counts the attempt)");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  MqttBase::sendResult = false;            // force the underlying send to fail
  deliver(mc, R"({"cmd":"nope"})");        // unknown cmd -> NACK attempt fails
  IS_EQUAL(MqttBase::responseCount, 1);
  END_IT
}

bool test_file_piece_without_begin_is_nacked() {
  IT("a file piece with no preceding begin is NACKed");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  deliver(mc, R"({"piece":0,"data":"YWJj"})");  // no begin -> storeBase64 fails
  IS_TRUE(MqttBase::lastResponse == MqttBase::Response::NACK);
  END_IT
}

bool test_a_binid_that_only_starts_with_the_env_name_is_rejected() {
  IT("a binId that merely begins with this environment's name is rejected");
  resetEnv();
  Connectivity conn;
  MqttCommon mc(conn, "common");
  // The env here is "native_test"; the comparison has to reach the terminator, or every longer
  // id sharing the prefix would be accepted and streamed into the Updater.
  deliver(mc, R"({"binId":"native_test_v2","name":"espFirmware","fileSize":3,"md5":"900150983cd24fb0d6963f7d28e17f72"})");
  IS_EQUAL(MqttBase::lastResponse, MqttBase::Response::NACK);
  END_IT
}

int main() {
  SUITE("MqttCommon");
  test_init_returns_true();
  test_publish_discovery_returns_true();
  test_response_send_failure_is_handled();
  test_file_piece_without_begin_is_nacked();
  test_cmd_reboot();
  test_cmd_unknown();
  test_file_begin_valid();
  test_file_begin_disallowed_name();
  test_fw_begin_wrong_binid();
  test_fw_begin_correct_binid();
  test_file_piece_stored();
  test_unknown_json_no_response();
  test_end_to_end_file_routes_to_ota_target();
  test_firmware_without_a_binid_is_rebooted_into();
  test_a_config_file_carrying_a_binid_is_not_rebooted_into();
  test_a_binid_that_only_starts_with_the_env_name_is_rejected();
  FINISH
}
