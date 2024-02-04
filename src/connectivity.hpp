#ifndef CONNECTIVITY_HPP
#define CONNECTIVITY_HPP

#include <Arduino.h>                          /// Arduino libraries header.
#ifdef ESP8266
#include <ESP8266WiFi.h>                      /// Wifi driver.
#include <ENC28J60lwIP.h>                     /// Ethernet driver.
#elif defined ESP32
#include <WiFi.h>
#include <ETH.h>
#endif
#include <WiFiClientSecure.h>                 /// TCP client with SSL.
#include <PubSubClient.h>                     /// MQTT client.
#include <HardwareSerial.h>
#include <functional>
#include <Ticker.h>                           /// Timer interrupt hadnler.
#include "server.hpp"
#include <vector>

class Connectivity final {
public:
  class MqttComBase;

  enum class Interface : uint8_t {
    WIFI = 0,
    ETHERNET,
    UNKNOWN
  };

  Connectivity(HardwareSerial& serial, uint8_t ethCS, uint8_t dbgLedPin, bool dbgLedOnState);

  /// @brief Destructor of the object.
  virtual ~Connectivity() = default;

  void begin(Interface interface, bool errorHandling);

  void loop();

  static bool getConnectionState();

private:
  inline bool loopSimple();

  inline bool beginSimple(Interface interface);

  inline bool startWifi();

  bool connect();

  void receiveMqttMessage(const char* topic, uint8_t* payload, uint32_t length);

  void sendMqttMessage(const char* subTopic, const char* payload);

  static const char* getISODateTime();

  bool registerCallback(Connectivity::MqttComBase* obj);

  const char* getIntStatusStr(wl_status_t status);

  const char* getMqttStatusStr(int8_t status);

#ifdef ESP32
  static void WiFiEvent(WiFiEvent_t event);
#endif

public:
  Connectivity(const Connectivity&) = delete;                       // Define copy constructor.
  Connectivity& operator=(const Connectivity&) = delete;            // Define copy assignment operator.
  Connectivity(Connectivity&&) = delete;                            // Define move constructor.
  Connectivity& operator=(Connectivity&&) = delete;                 // Define move assignment operator.

private:
  struct __attribute__((packed))
  MqttCredentials {
    char userName[sizeof(mqttSettings::userName)];
    char password[sizeof(mqttSettings::password)];
    char serverName[sizeof(mqttSettings::serverName)];
    uint16_t serverPort;
    char clientName[36];
    char senderTopic[32];
    char receiverTopic[32];
    MqttCredentials() : userName{'\0'}, password{'\0'}, serverName{'\0'}, serverPort(0), clientName{'\0'}, senderTopic{'\0'}, receiverTopic{'\0'} {}
  };

  HardwareSerial& serialPort;
#ifdef ESP8266
  ENC28J60lwIP ethInt;
#elif defined ESP32
  static constexpr uint8_t ETH_PHY_ADDR_ = 1;                 // I²C-address of Ethernet PHY (0 or 1 for LAN8720, 31 for TLK110)
  static constexpr int8_t ETH_PHY_POWER_ = 17;                // Pin# of the enable signal for the external crystal oscillator (-1 to disable for internal APLL source)
  static constexpr int8_t ETH_PHY_MDC_ = 23;                  // Pin# of the I²C clock signal for the Ethernet PHY
  static constexpr int8_t ETH_PHY_MDIO_ = 18;                 // Pin# of the I²C IO signal for the Ethernet PHY
  static constexpr auto ETH_PHY_TYPE_ = ETH_PHY_LAN8720;      // Type of the Ethernet PHY (LAN8720 or TLK110)
  static constexpr auto ETH_CLK_MODE_ = ETH_CLOCK_GPIO0_IN;
  //ETH_CLOCK_GPIO0_IN   - default: external clock from crystal oscillator
  //ETH_CLOCK_GPIO0_OUT  - 50MHz clock from internal APLL output on GPIO0 - possibly an inverter is needed for LAN8720
  //ETH_CLOCK_GPIO16_OUT - 50MHz clock from internal APLL output on GPIO16 - possibly an inverter is needed for LAN8720
  //ETH_CLOCK_GPIO17_OUT - 50MHz clock from internal APLL inverted output on GPIO17 - tested with LAN8720
  static bool ethConnected;
#endif
  WiFiClientSecure tcpClient;
  PubSubClient mqttClient;
  Interface usedInterface;
  wl_status_t interfaceStatus;
  MqttCredentials mqttCredentials;
  int8_t mqttState;
  static bool isDeviceOnline;
  const uint32_t cppVersion;
  const uint16_t fwVersion;
  const uint32_t gitHash;
  static constexpr uint8_t macStringSize = 13;
  std::vector<Connectivity::MqttComBase*> messageMap;
  static constexpr uint32_t deviceResetTime = 3 * 60 * 60 * 1000;

public:
  static const char PROGMEM OK_STATE[];
  static const char PROGMEM ERR_STATE[];
private:
  static const char PROGMEM wifiFileLocation[];
  static const char PROGMEM BASE_TOPIC[];
  static const char PROGMEM SENDER_TOPIC[];
  static const char PROGMEM RECEIVER_TOPIC[];
  static const char PROGMEM DEVICE_TYPE[];
  static const char PROGMEM INIT_PREFIX[];
  static const char PROGMEM FS_PREFIX[];
  static const char PROGMEM ETH_PREFIX[];
  static const char PROGMEM WIFI_PREFIX[];
  static const char PROGMEM NTP_PREFIX[];
  static const char PROGMEM JSON_PREFIX[];
  static const char PROGMEM TCP_PREFIX[];
  static const char PROGMEM MQTT_PREFIX[];
  static const char PROGMEM RUN_PREFIX[];

  static const char PROGMEM WL_NO_SHIELD_STR[];
  static const char PROGMEM WL_IDLE_STATUS_STR[];
  static const char PROGMEM WL_NO_SSID_AVAIL_STR[];
  static const char PROGMEM WL_SCAN_COMPLETED_STR[];
  static const char PROGMEM WL_CONNECTED_STR[];
  static const char PROGMEM WL_CONNECT_FAILED_STR[];
  static const char PROGMEM WL_CONNECTION_LOST_STR[];
  static const char PROGMEM WL_WRONG_PASSWORD_STR[];
  static const char PROGMEM WL_DISCONNECTED_STR[];
  static const char PROGMEM WL_UNKNOWN_STATUS_STR[];
  static const char PROGMEM MQTT_CONNECTION_TIMEOUT_STR[];
  static const char PROGMEM MQTT_CONNECTION_LOST_STR[];
  static const char PROGMEM MQTT_CONNECT_FAILED_STR[];
  static const char PROGMEM MQTT_DISCONNECTED_STR[];
  static const char PROGMEM MQTT_CONNECTED_STR[];
  static const char PROGMEM MQTT_CONNECT_BAD_PROTOCOL_STR[];
  static const char PROGMEM MQTT_CONNECT_BAD_CLIENT_ID_STR[];
  static const char PROGMEM MQTT_CONNECT_UNAVAILABLE_STR[];
  static const char PROGMEM MQTT_CONNECT_BAD_CREDENTIALS_STR[];
  static const char PROGMEM MQTT_CONNECT_UNAUTHORIZED_STR[];
  static const char PROGMEM MQTT_UNKNOWN_STATUS_STR[];

private:
  class WdtWrapper final {
  public:
    WdtWrapper() = default;
    ~WdtWrapper() = default;

    void enableHwWdt();
    void resetHwWdt();
    void resetHwWdtIfPossible();
    void setEnabledResetNumber(uint8_t enabledResetNumber);

    WdtWrapper(const WdtWrapper&) = delete;                       // Define copy constructor.
    WdtWrapper& operator=(const WdtWrapper&) = delete;            // Define copy assignment operator.
    WdtWrapper(WdtWrapper&&) = delete;                            // Define move constructor.
    WdtWrapper& operator=(WdtWrapper&&) = delete;                 // Define move assignment operator.

  private:
    uint8_t enabledResetNumber_ = 0;
  };
  WdtWrapper WdtHandler;

public:
  class DebugLED final {
  public:
    DebugLED(uint8_t ledPin, bool ledOnState);
    virtual ~DebugLED() = default;
    inline void ledOn();
    inline void ledOff();
    void startTicker(uint32_t tickInterval_ms);
    void stopTicker();

    DebugLED(const DebugLED&) = delete;                       // Define copy constructor.
    DebugLED& operator=(const DebugLED&) = delete;            // Define copy assignment operator.
    DebugLED(DebugLED&&) = delete;                            // Define move constructor.
    DebugLED& operator=(DebugLED&&) = delete;                 // Define move assignment operator.

  private:
    static IRAM_ATTR void ledToggle();
    inline void ledHigh();
    inline void ledLow();

    static uint8_t ledPin_;
    const bool ledOnState_;
    Ticker ledTicker;
  };
  DebugLED debugLed;

public:
  class TimeTracker final {
  public:
    explicit TimeTracker(uint32_t goalTime = 0);
    virtual ~TimeTracker() = default;
    void startTime();
    void resetTime();
    void setGoal(uint32_t goalTime);
    uint32_t stopTime();
    uint32_t getElapsedTime();
    bool isGoalReached();

    TimeTracker(const TimeTracker&) = delete;                       // Define copy constructor.
    TimeTracker& operator=(const TimeTracker&) = delete;            // Define copy assignment operator.
    TimeTracker(TimeTracker&&) = delete;                            // Define move constructor.
    TimeTracker& operator=(TimeTracker&&) = delete;                 // Define move assignment operator.

  private:
    uint32_t startTime_;
    uint32_t goalTime_;
  };
  TimeTracker timeTracker;
  TimeTracker loopTimeTracker;

public:
  class Crc32 final {
  public:
    explicit Crc32(uint32_t initValue = 0xFFFFFFFF, uint32_t polynomial = 0xEDB88320);
    ~Crc32() = default;

    void next(uint8_t value);

    void next(const uint8_t* values, uint32_t length);

    uint32_t get() const;

    static uint32_t calculate(const uint8_t *data, uint16_t length);

    Crc32(const Crc32&) = delete;                       // Define copy constructor.
    Crc32& operator=(const Crc32&) = delete;            // Define copy assignment operator.
    Crc32(Crc32&&) = delete;                            // Define move constructor.
    Crc32& operator=(Crc32&&) = delete;                 // Define move assignment operator.

  private:
    uint32_t crc_;                                      // CRC32 starting value.
    const uint32_t polynomial_;                         // CRC32 polynomial.
  };

public:
  /// @brief Base64 encoding and decoding of strings. Uses '+' for 62, '/' for 63, '=' for padding.
  class Base64 final {
  public:
    Base64() = delete;
    ~Base64() = delete;

  public:
    /// @brief Calculates length of base64 string needed for a given number of binary bytes.
    /// @param plainLength Amount of binary data in bytes.
    /// @return Number of base64 characters needed to encode input_length bytes of binary data.
    static uint32_t encodedLength(uint32_t plainLength);

    /// @brief Calculates number of bytes of binary data in a base64 string.
    /// @param input Base64-encoded null-terminated string.
    /// @param inputLength Number of bytes to read from input pointer.
    /// @return Number of bytes of binary data in input.
    static uint32_t decodedLength(const uint8_t input[], uint32_t inputLength);

    /// @brief Converts an array of bytes to a base64 null-terminated string.
    /// @param input Pointer to input data.
    /// @param output Pointer to output string. Null terminator will be added automatically.
    /// @param inputLength Number of bytes to read from input pointer.
    /// @return Length of encoded string in bytes (not including null terminator).
    static uint32_t encodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength);

    /// @brief Converts a base64 null-terminated string to an array of bytes.
    /// @param input Pointer to input string.
    /// @param output Pointer to output array.
    /// @param inputLength - Number of bytes to read from input pointer.
    /// @return Number of bytes in the decoded binary.
    static uint32_t decodeBase64(const uint8_t input[], uint8_t output[], uint32_t inputLength);

  private:
    static inline void fromA3ToA4(uint8_t* A4, const uint8_t* A3);
    static inline void fromA4ToA3(uint8_t* A3, const uint8_t* A4);
    static inline uint8_t lookupTable(char c);

  public:
    Base64(const Base64&) = delete;                       // Define copy constructor.
    Base64& operator=(const Base64&) = delete;            // Define copy assignment operator.
    Base64(Base64&&) = delete;                            // Define move constructor.
    Base64& operator=(Base64&&) = delete;                 // Define move assignment operator.

  private:
    static const char PROGMEM base64AlphabetTable_[];
  };

private:
  class DataTransfer final {
  public:
    explicit DataTransfer(Stream* serial = nullptr);

    /// @brief Destructor of the object.
    virtual ~DataTransfer() = default;

    bool begin(uint32_t fileSize, uint32_t fileCrc, const char* fileName);

  private:
    bool stop(bool deleteFile);

  public:
    bool storeBase64(uint32_t filePieceNumber, const char* fileData);

    bool store(uint32_t filePieceNumber, const uint8_t* fileData, uint16_t fileDataSize);

    bool checkValidity();

    DataTransfer(const DataTransfer&) = delete;                       // Define copy constructor.
    DataTransfer& operator=(const DataTransfer&) = delete;            // Define copy assignment operator.
    DataTransfer(DataTransfer&&) = delete;                            // Define move constructor.
    DataTransfer& operator=(DataTransfer&&) = delete;                 // Define move assignment operator.

  private:
    Stream* serialPort;
    uint32_t fileSize_;
    uint32_t fileCrc_;
    uint32_t nextFilePieceNumber_;
    uint32_t remainingFileSize_;
    const char* fileName_;
    bool fileTransferStarted_;
    static constexpr uint16_t receivedFilePieceSize = 336; // It should always be divisible by both 3 and 4!
    static const char PROGMEM FILE_TRANSFER_PREFIX[];
  public:
    static const char PROGMEM otaFwLocation[];
    static const char PROGMEM wifiTempFileLocation[];
  };
  DataTransfer dataTransfer;

public:
  class MqttComBase {
  public:
    enum class Response : uint8_t {
      NACK = 0,
      ACK,
    };
  protected:
    MqttComBase(Connectivity& connectivity, const char* classID);
    virtual ~MqttComBase() = default;
    void messageSend(const char* payload) const;
    virtual bool sendResponse(Response resp, uint16_t cmd);
    const char* getIsoTime();
  public:
    virtual void messageReceived(uint8_t* payload, uint32_t length) = 0;
    virtual bool begin() = 0;
    virtual bool loop() = 0;
    const char* getClassId() const;

    MqttComBase(const MqttComBase&) = delete;                       // Define copy constructor.
    MqttComBase& operator=(const MqttComBase&) = delete;            // Define copy assignment operator.
    MqttComBase(MqttComBase&&) = delete;                            // Define move constructor.
    MqttComBase& operator=(MqttComBase&&) = delete;                 // Define move assignment operator.
  protected:
    Connectivity& conn;
  private:
    char classId[16];
  };

private:
  class Common final : public Connectivity::MqttComBase {
  public:
    enum class Command : uint8_t {
      BLANK = 0,
      RESTART,
      FW_DT_START,
      FW_DT_DATA,
      FW_DT_END,
      WIFICFG_DT_START,
      WIFICFG_DT_DATA,
      WIFICFG_DT_END,
      EXT_FILE_DT_START,
      EXT_FILE_DT_DATA,
      EXT_FILE_DT_END
    };

    Common(Connectivity& connectivity, const char* classID);

    /// @brief Destructor of the object.
    virtual ~Common() = default;

    virtual void messageReceived(uint8_t* payload, uint32_t length) override;

    virtual bool begin() override;

    virtual bool loop() override;

    inline void messageSend(const char* payload) const;

    /// @brief Reset the MCU.
    void restartESP();

  public:
    Common(const Common&) = delete;                       // Define copy constructor.
    Common& operator=(const Common&) = delete;            // Define copy assignment operator.
    Common(Common&&) = delete;                            // Define move constructor.
    Common& operator=(Common&&) = delete;                 // Define move assignment operator.
  private:
    char externalFileName[28];
    static const char PROGMEM COMMON_PREFIX[];
  };
  Common common;
};

#endif