#include "configHandler.hpp"
#include "Arduino.h"
#include "LittleFS.h"
#include "BDDTest.h"
#include "PubSubClient.h"
#include "ShimClient.h"
#include <string>
#include <string.h>

// configHandler reads Wi-Fi + server credentials from the same server.json, and the CA cert from
// its own path; the LittleFS shim serves both from an in-memory map keyed by those paths.
static const char* credPath() { return FileName::getMqttServerCredentialsLocation(); }
static const char* certPath() { return FileName::getMqttServerCertLocation(); }

// Error bits (mirror of the private ConfigHandler error enums).
namespace WifiErr {
  // clang-format off
  constexpr uint8_t FILE_OPEN   = 1U << 0U;
  constexpr uint8_t JSON_PARSE  = 1U << 1U;
  constexpr uint8_t NO_SSID     = 1U << 2U;
  constexpr uint8_t NO_PWD      = 1U << 3U;
  constexpr uint8_t SSID_LEN    = 1U << 4U;
  constexpr uint8_t PWD_LEN     = 1U << 5U;
  // clang-format on
}  // namespace WifiErr
namespace CredErr {
  // clang-format off
  constexpr uint16_t NO_PORT    = 1U << 5U;
  constexpr uint16_t USER_LEN   = 1U << 6U;
  constexpr uint16_t PASS_LEN   = 1U << 7U;
  constexpr uint16_t URL_LEN    = 1U << 8U;
  constexpr uint16_t PORT_NUM   = 1U << 9U;
  // clang-format on
}  // namespace CredErr
namespace CertErr {
  // clang-format off
  constexpr uint8_t FILE_OPEN   = 1U << 0U;
  constexpr uint8_t EMPTY       = 1U << 1U;
  constexpr uint8_t STORING     = 1U << 2U;
  // clang-format on
}  // namespace CertErr

static const char* kValidServerJson =
    R"({"ssid":"MyNet","password":"secretpw","mqttUserName":"user","mqttPassword":"pass",)"
    R"("mqttServerUrl":"mqtt.example.com","mqttServerPort":8883})";

// ---- initialiseFileSystem ----

bool test_init_fs_succeeds() {
  IT("initialiseFileSystem returns true and reports zeroed sizes on host");
  LittleFS.reset();
  size_t total = 9U;
  size_t used = 9U;
  size_t freeB = 9U;
  IS_TRUE(ConfigHandler::initialiseFileSystem(total, used, freeB));
  IS_EQUAL(total, 0U);
  IS_EQUAL(used, 0U);
  IS_EQUAL(freeB, 0U);
  END_IT
}

bool test_init_fs_fails_when_begin_fails() {
  IT("initialiseFileSystem returns false when the file system fails to mount");
  LittleFS.reset();
  LittleFS.setBeginResult(false);
  size_t total = 0U;
  size_t used = 0U;
  size_t freeB = 0U;
  IS_FALSE(ConfigHandler::initialiseFileSystem(total, used, freeB));
  LittleFS.reset();
  END_IT
}

// ---- loadJsonFile ----

bool test_load_json_file_open_failed() {
  IT("loadJsonFile returns FileOpenFailed when the file is absent");
  LittleFS.reset();
  JsonDocument doc;
  IS_TRUE(ConfigHandler::loadJsonFile("/missing.json", doc) == ConfigHandler::JsonLoadResult::FileOpenFailed);
  END_IT
}

bool test_load_json_parse_failed() {
  IT("loadJsonFile returns ParseFailed on malformed JSON");
  LittleFS.reset();
  LittleFS.setFile("/bad.json", "{not valid");
  JsonDocument doc;
  IS_TRUE(ConfigHandler::loadJsonFile("/bad.json", doc) == ConfigHandler::JsonLoadResult::ParseFailed);
  END_IT
}

bool test_load_json_ok() {
  IT("loadJsonFile returns Ok and populates the document on valid JSON");
  LittleFS.reset();
  LittleFS.setFile("/ok.json", R"({"a":42})");
  JsonDocument doc;
  IS_TRUE(ConfigHandler::loadJsonFile("/ok.json", doc) == ConfigHandler::JsonLoadResult::Ok);
  IS_EQUAL(doc["a"].as<int>(), 42);
  END_IT
}

// ---- getJsonValue ----

bool test_get_json_value_ok() {
  IT("getJsonValue returns true and the value for a present, correctly-typed key");
  LittleFS.reset();
  LittleFS.setFile("/cfg.json", R"({"flag":true})");
  bool flag = false;
  IS_TRUE(ConfigHandler::getJsonValue<bool>("/cfg.json", "flag", flag));
  IS_TRUE(flag);
  END_IT
}

bool test_get_json_value_wrong_type() {
  IT("getJsonValue returns false and keeps the caller's value when the key has the wrong type");
  LittleFS.reset();
  LittleFS.setFile("/cfg.json", R"({"flag":"yes"})");
  // Started at true so a clobbered output is visible: callers rely on their own default
  // surviving a failed read, which is how an optional setting keeps its documented default.
  bool flag = true;
  IS_FALSE(ConfigHandler::getJsonValue<bool>("/cfg.json", "flag", flag));
  IS_TRUE(flag);
  END_IT
}

bool test_get_json_value_missing_key() {
  IT("getJsonValue returns false and keeps the caller's value when the key is absent");
  LittleFS.reset();
  LittleFS.setFile("/cfg.json", R"({"other":1})");
  bool flag = true;
  IS_FALSE(ConfigHandler::getJsonValue<bool>("/cfg.json", "flag", flag));
  IS_TRUE(flag);
  END_IT
}

bool test_get_json_value_file_missing() {
  IT("getJsonValue returns false and keeps the caller's value when the file cannot be opened");
  LittleFS.reset();
  bool flag = true;
  IS_FALSE(ConfigHandler::getJsonValue<bool>("/none.json", "flag", flag));
  IS_TRUE(flag);
  END_IT
}

// ---- getWifiConfig ----

bool test_wifi_config_ok() {
  IT("getWifiConfig returns 0 and fills ssid/password from valid JSON");
  LittleFS.reset();
  LittleFS.setFile(credPath(), kValidServerJson);
  char ssid[ConfigHandler::getMaxWifiSsidSize()] = { '\0' };
  char password[ConfigHandler::getMaxWifiPasswordSize()] = { '\0' };
  IS_EQUAL(ConfigHandler::getWifiConfig(ssid, password), 0U);
  IS_TRUE(strcmp(ssid, "MyNet") == 0);
  IS_TRUE(strcmp(password, "secretpw") == 0);
  END_IT
}

bool test_wifi_config_file_open_failed() {
  IT("getWifiConfig reports FILE_OPEN_FAILED when the file is absent");
  LittleFS.reset();
  char ssid[ConfigHandler::getMaxWifiSsidSize()] = { '\0' };
  char password[ConfigHandler::getMaxWifiPasswordSize()] = { '\0' };
  IS_EQUAL(ConfigHandler::getWifiConfig(ssid, password), WifiErr::FILE_OPEN);
  END_IT
}

bool test_wifi_config_parse_failed() {
  IT("getWifiConfig reports JSON_PARSING_ERROR on malformed JSON");
  LittleFS.reset();
  LittleFS.setFile(credPath(), "{broken");
  char ssid[ConfigHandler::getMaxWifiSsidSize()] = { '\0' };
  char password[ConfigHandler::getMaxWifiPasswordSize()] = { '\0' };
  IS_EQUAL(ConfigHandler::getWifiConfig(ssid, password), WifiErr::JSON_PARSE);
  END_IT
}

bool test_wifi_config_missing_keys() {
  IT("getWifiConfig reports both missing-key errors when ssid and password are absent");
  LittleFS.reset();
  LittleFS.setFile(credPath(), R"({"other":1})");
  char ssid[ConfigHandler::getMaxWifiSsidSize()] = { '\0' };
  char password[ConfigHandler::getMaxWifiPasswordSize()] = { '\0' };
  IS_EQUAL(ConfigHandler::getWifiConfig(ssid, password),
           static_cast<uint8_t>(WifiErr::NO_SSID | WifiErr::NO_PWD));
  END_IT
}

bool test_wifi_config_ssid_too_long() {
  IT("getWifiConfig reports SSID_LENGTH_ERR when the ssid does not fit the buffer");
  LittleFS.reset();
  LittleFS.setFile(credPath(), R"({"ssid":"0123456789abcdef0123456789abcdef0","password":"secretpw"})");
  char ssid[ConfigHandler::getMaxWifiSsidSize()] = { '\0' };
  char password[ConfigHandler::getMaxWifiPasswordSize()] = { '\0' };
  IS_EQUAL(ConfigHandler::getWifiConfig(ssid, password), WifiErr::SSID_LEN);
  END_IT
}

bool test_wifi_config_maximum_passphrase() {
  IT("getWifiConfig preserves a 32-byte SSID and 63-byte passphrase with their terminators");
  LittleFS.reset();
  LittleFS.setFile(credPath(), R"({"ssid":"0123456789abcdef0123456789abcdef","password":"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!"})");
  char ssid[ConfigHandler::getMaxWifiSsidSize()];
  char password[ConfigHandler::getMaxWifiPasswordSize()];
  memset(ssid, '!', sizeof(ssid));
  memset(password, '!', sizeof(password));
  IS_EQUAL(ConfigHandler::getWifiConfig(ssid, password), 0U);
  IS_TRUE(strcmp(ssid, "0123456789abcdef0123456789abcdef") == 0);
  IS_TRUE(strcmp(password, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!") == 0);
  IS_EQUAL(ssid[sizeof(ssid) - 1U], '\0');
  IS_EQUAL(password[sizeof(password) - 1U], '\0');
  END_IT
}

bool test_wifi_config_utf8_ssid() {
  IT("getWifiConfig accepts a 32-byte UTF-8 SSID without splitting a character");
  LittleFS.reset();
  LittleFS.setFile(credPath(), R"({"ssid":"éééééééééééééééé","password":"secretpw"})");
  char ssid[ConfigHandler::getMaxWifiSsidSize()] = { '\0' };
  char password[ConfigHandler::getMaxWifiPasswordSize()] = { '\0' };
  IS_EQUAL(ConfigHandler::getWifiConfig(ssid, password), 0U);
  IS_TRUE(strcmp(ssid, "éééééééééééééééé") == 0);
  END_IT
}

bool test_wifi_config_rejects_invalid_strings() {
  IT("getWifiConfig rejects overlong strings and embedded NULs without changing outputs");
  struct InvalidConfig {
    const char* json;
    uint8_t error;
  };
  const InvalidConfig cases[] = {
    { R"({"ssid":"ééééééééééééééééé","password":"secretpw"})", WifiErr::SSID_LEN },
    { R"({"ssid":"wifi","password":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"})", WifiErr::PWD_LEN },
    { R"({"ssid":"wifi","password":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg"})", WifiErr::PWD_LEN },
    { R"({"ssid":"wifi\u0000hidden","password":"secretpw"})", WifiErr::SSID_LEN },
    { R"({"ssid":"wifi","password":"secretpw\u0000hidden"})", WifiErr::PWD_LEN },
    { R"({"ssid":"","password":"secretpw"})", WifiErr::SSID_LEN },
    { R"({"ssid":"wifi","password":""})", WifiErr::PWD_LEN }
  };
  for(const InvalidConfig& config : cases) {
    LittleFS.reset();
    LittleFS.setFile(credPath(), config.json);
    char ssid[ConfigHandler::getMaxWifiSsidSize()];
    char password[ConfigHandler::getMaxWifiPasswordSize()];
    memset(ssid, '!', sizeof(ssid));
    memset(password, '!', sizeof(password));
    IS_EQUAL(ConfigHandler::getWifiConfig(ssid, password), config.error);
    IS_EQUAL(ssid[0], '!');
    IS_EQUAL(password[0], '!');
  }
  END_IT
}

// ---- getServerCredentials ----

static std::string serverJson(const std::string& user, const std::string& pass, const std::string& host) {
  JsonDocument doc;
  doc["mqttUserName"] = user;
  doc["mqttPassword"] = pass;
  doc["mqttServerUrl"] = host;
  doc["mqttServerPort"] = 8883U;
  std::string json;
  serializeJson(doc, json);
  return json;
}

bool test_server_creds_ok() {
  IT("getServerCredentials returns 0 and fills every field from valid JSON");
  LittleFS.reset();
  LittleFS.setFile(credPath(), kValidServerJson);
  char user[ConfigHandler::getMaxMqttUserNameSize()] = { '\0' };
  char pass[ConfigHandler::getMaxMqttPasswordSize()] = { '\0' };
  char url[ConfigHandler::getMaxMqttServerUrlSize()] = { '\0' };
  uint16_t port = 0U;
  IS_EQUAL(ConfigHandler::getServerCredentials(user, pass, url, port), 0U);
  IS_TRUE(strcmp(user, "user") == 0);
  IS_TRUE(strcmp(pass, "pass") == 0);
  IS_TRUE(strcmp(url, "mqtt.example.com") == 0);
  IS_EQUAL(port, 8883U);
  END_IT
}

bool test_server_creds_port_wrong_type() {
  IT("getServerCredentials reports MISSING_MQTT_PORT when the port is not a number");
  LittleFS.reset();
  LittleFS.setFile(credPath(), R"({"mqttUserName":"u","mqttPassword":"p","mqttServerUrl":"h","mqttServerPort":"8883"})");
  char user[ConfigHandler::getMaxMqttUserNameSize()] = { '\0' };
  char pass[ConfigHandler::getMaxMqttPasswordSize()] = { '\0' };
  char url[ConfigHandler::getMaxMqttServerUrlSize()] = { '\0' };
  uint16_t port = 0U;
  IS_EQUAL(ConfigHandler::getServerCredentials(user, pass, url, port), CredErr::NO_PORT);
  END_IT
}

bool test_server_creds_port_zero() {
  IT("getServerCredentials reports MQTT_PORT_NUM_ERR when the port is 0");
  LittleFS.reset();
  LittleFS.setFile(credPath(), R"({"mqttUserName":"u","mqttPassword":"p","mqttServerUrl":"h","mqttServerPort":0})");
  char user[ConfigHandler::getMaxMqttUserNameSize()] = { '\0' };
  char pass[ConfigHandler::getMaxMqttPasswordSize()] = { '\0' };
  char url[ConfigHandler::getMaxMqttServerUrlSize()] = { '\0' };
  uint16_t port = 0U;
  IS_EQUAL(ConfigHandler::getServerCredentials(user, pass, url, port), CredErr::PORT_NUM);
  END_IT
}

bool test_server_creds_url_too_long() {
  IT("getServerCredentials reports MQTT_URL_LENGTH_ERR when the url does not fit");
  LittleFS.reset();
  LittleFS.setFile(credPath(), serverJson("u", "p", std::string(65U, 'h')));
  char user[ConfigHandler::getMaxMqttUserNameSize()] = { '\0' };
  char pass[ConfigHandler::getMaxMqttPasswordSize()] = { '\0' };
  char url[ConfigHandler::getMaxMqttServerUrlSize()] = { '\0' };
  uint16_t port = 0U;
  IS_EQUAL(ConfigHandler::getServerCredentials(user, pass, url, port), CredErr::URL_LEN);
  END_IT
}

bool test_server_creds_maximum_lengths() {
  IT("getServerCredentials preserves full-length values and fits them in a CONNECT with a will");
  LittleFS.reset();
  std::string userValue;
  for(size_t i = 0U; i < 24U; i++) { userValue += "é"; }
  const std::string passValue(48U, 'p');
  const std::string hostValue = std::string(56U, 'h') + ".example";
  LittleFS.setFile(credPath(), serverJson(userValue, passValue, hostValue));
  char user[ConfigHandler::getMaxMqttUserNameSize()];
  char pass[ConfigHandler::getMaxMqttPasswordSize()];
  char url[ConfigHandler::getMaxMqttServerUrlSize()];
  memset(user, '!', sizeof(user));
  memset(pass, '!', sizeof(pass));
  memset(url, '!', sizeof(url));
  uint16_t port = 0U;
  IS_EQUAL(ConfigHandler::getServerCredentials(user, pass, url, port), 0U);
  IS_EQUAL(user[sizeof(user) - 1U], '\0');
  IS_EQUAL(pass[sizeof(pass) - 1U], '\0');
  IS_EQUAL(url[sizeof(url) - 1U], '\0');
  IS_TRUE(userValue == user);
  IS_TRUE(passValue == pass);
  IS_TRUE(hostValue == url);
  IS_EQUAL(port, 8883U);

  ShimClient shimClient;
  shimClient.setAllowConnect(true);
  shimClient.expectConnect(url, port);
  const uint8_t connack[] = { 0x20U, 0x02U, 0x00U, 0x00U };
  shimClient.respond(connack, sizeof(connack));
  PubSubClient client(shimClient);
  client.setServer(url, port);
  const std::string clientId(31U, 'c');
  IS_TRUE(client.connect(clientId.c_str(), user, pass, "iot/dtos/a1b2c3d4e5f6/availability", 1U, true, R"({"state":"offline"})"));
  IS_FALSE(shimClient.error());
  IS_EQUAL(shimClient.received(), 203U);
  END_IT
}

bool test_server_creds_rejects_invalid_strings() {
  IT("getServerCredentials rejects invalid strings without changing any output");
  struct InvalidField {
    uint8_t index;
    size_t maximum;
    uint16_t error;
  };
  const InvalidField fields[] = {
    { 0U, 48U, CredErr::USER_LEN },
    { 1U, 48U, CredErr::PASS_LEN },
    { 2U, 64U, CredErr::URL_LEN }
  };
  for(const InvalidField& field : fields) {
    const std::string invalidValues[] = { std::string(field.maximum + 1U, 'x'), std::string("valid\0hidden", 12U), "" };
    for(const std::string& value : invalidValues) {
      std::string values[] = { "user", "pass", "broker.example.com" };
      values[field.index] = value;
      LittleFS.reset();
      LittleFS.setFile(credPath(), serverJson(values[0], values[1], values[2]));
      char user[ConfigHandler::getMaxMqttUserNameSize()];
      char pass[ConfigHandler::getMaxMqttPasswordSize()];
      char url[ConfigHandler::getMaxMqttServerUrlSize()];
      memset(user, '!', sizeof(user));
      memset(pass, '!', sizeof(pass));
      memset(url, '!', sizeof(url));
      uint16_t port = 1234U;
      IS_EQUAL(ConfigHandler::getServerCredentials(user, pass, url, port), field.error);
      IS_TRUE(std::string(user, sizeof(user)) == std::string(sizeof(user), '!'));
      IS_TRUE(std::string(pass, sizeof(pass)) == std::string(sizeof(pass), '!'));
      IS_TRUE(std::string(url, sizeof(url)) == std::string(sizeof(url), '!'));
      IS_EQUAL(port, 1234U);
    }
  }
  END_IT
}

// ---- getServerCert ----

static size_t g_certSeenSize;
static bool storeCertOk(File& cert, size_t size) {
  g_certSeenSize = size;
  (void)cert;
  return true;
}
static bool storeCertFail(File& cert, size_t size) {
  (void)cert;
  (void)size;
  return false;
}

bool test_server_cert_ok() {
  IT("getServerCert returns 0 and hands the cert stream + size to the callback");
  LittleFS.reset();
  const char* pem = "-----BEGIN CERTIFICATE-----\nabc\n-----END CERTIFICATE-----\n";
  LittleFS.setFile(certPath(), pem);
  g_certSeenSize = 0U;
  IS_EQUAL(ConfigHandler::getServerCert(storeCertOk), 0U);
  IS_EQUAL(g_certSeenSize, strlen(pem));
  END_IT
}

bool test_server_cert_file_missing() {
  IT("getServerCert reports FILE_OPEN_FAILED when the cert file is absent");
  LittleFS.reset();
  IS_EQUAL(ConfigHandler::getServerCert(storeCertOk), CertErr::FILE_OPEN);
  END_IT
}

bool test_server_cert_empty() {
  IT("getServerCert reports CERT_FILE_EMPTY for a zero-length cert file");
  LittleFS.reset();
  LittleFS.setFile(certPath(), "");
  IS_EQUAL(ConfigHandler::getServerCert(storeCertOk), CertErr::EMPTY);
  END_IT
}

bool test_server_cert_storing_failed() {
  IT("getServerCert reports CERT_STORING_FAILED when the callback fails");
  LittleFS.reset();
  LittleFS.setFile(certPath(), "cert-bytes");
  IS_EQUAL(ConfigHandler::getServerCert(storeCertFail), CertErr::STORING);
  END_IT
}

int main() {
  SUITE("ConfigHandler");
  test_init_fs_succeeds();
  test_init_fs_fails_when_begin_fails();
  test_load_json_file_open_failed();
  test_load_json_parse_failed();
  test_load_json_ok();
  test_get_json_value_ok();
  test_get_json_value_wrong_type();
  test_get_json_value_missing_key();
  test_get_json_value_file_missing();
  test_wifi_config_ok();
  test_wifi_config_file_open_failed();
  test_wifi_config_parse_failed();
  test_wifi_config_missing_keys();
  test_wifi_config_ssid_too_long();
  test_wifi_config_maximum_passphrase();
  test_wifi_config_utf8_ssid();
  test_wifi_config_rejects_invalid_strings();
  test_server_creds_ok();
  test_server_creds_port_wrong_type();
  test_server_creds_port_zero();
  test_server_creds_url_too_long();
  test_server_creds_maximum_lengths();
  test_server_creds_rejects_invalid_strings();
  test_server_cert_ok();
  test_server_cert_file_missing();
  test_server_cert_empty();
  test_server_cert_storing_failed();
  FINISH
}
