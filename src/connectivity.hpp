#ifndef CONNECTIVITY_HPP
#define CONNECTIVITY_HPP

#include <Arduino.h>                          /// Arduino libraries header.
#include <ESP8266WiFi.h>                      /// Wifi driver.
#include <ENC28J60lwIP.h>                     /// Ethernet driver.
#include <WiFiClientSecure.h>                 /// TCP client with SSL.
#include <PubSubClient.h>                     /// MQTT client.
#include <functional>
#include <Ticker.h>                           /// Timer interrupt hadnler.

class Connectivity {
public:
  class MqttComBase;

private:
  class Common;

public:
  enum class Interface : uint8_t {
    WIFI = 0,
    ETHERNET,
    UNKNOWN
  };

  enum class ConfigFile : uint8_t {
    NORMAL = 0,
    BACKUP
  };

  enum class CertFile : uint8_t {
    NORMAL = 0,
    BACKUP
  };

  Connectivity(Stream* serial = nullptr, const uint8_t ethCS = D8, uint8_t dbgLedPin = D4, bool dbgLedOnState = HIGH);

  /// @brief Destructor of the object.
  virtual ~Connectivity() = default;

  bool begin(Interface interface = Interface::ETHERNET);

  bool startWifi();

  bool checkFiles();

  bool loadConfig(ConfigFile actualConfig = ConfigFile::NORMAL);

  bool connect(CertFile actualCert = CertFile::NORMAL);

  bool loop();

  void receiveMqttMessage(const char* topic, uint8_t* payload, uint32_t length);

  void sendMqttMessage(const char* subTopic, const char* payload);

  String getISODateTime();

  static bool registerCallback(Connectivity::MqttComBase* obj);

  Connectivity(const Connectivity&) = delete;                       // Define copy constructor.
  Connectivity& operator=(const Connectivity&) = delete;            // Define copy assignment operator.
  Connectivity(Connectivity&&) = delete;                            // Define move constructor.
  Connectivity& operator=(Connectivity&&) = delete;                 // Define move assignment operator.

private:
  struct __attribute__((packed))
  MqttCredentials {
    char userName[24];
    char password[24];
    char serverName[34];
    uint16_t serverPort;
    char clientName[28];
    char senderTopic[32];
    char receiverTopic[32];
    MqttCredentials() : userName{'\0'}, password{'\0'}, serverName{'\0'}, serverPort(0), clientName{'\0'}, senderTopic{'\0'}, receiverTopic{'\0'} {}
  };

  Stream* serialPort;
  ENC28J60lwIP ethInt;
  WiFiClientSecure tcpClient;
  PubSubClient mqttClient;
  Interface usedInterface;
  wl_status_t interfaceStatus;
  MqttCredentials mqttCredentials;
  int8_t mqttState;
  const uint32_t cppVersion;
  const uint16_t fwVersion;
  const uint32_t gitHash;

  static Connectivity::MqttComBase* messageMap[10];
  static uint8_t messageMapPointer;

private:
  static const char PROGMEM wifiFileLocation[];
  static const char PROGMEM configFileLocation[];
  static const char PROGMEM configBackupFileLocation[];
  static const char PROGMEM certFileLocation[];
  static const char PROGMEM certBackupFileLocation[];
  static const char PROGMEM BASE_TOPIC[];
  static const char PROGMEM SENDER_TOPIC[];
  static const char PROGMEM RECEIVER_TOPIC[];
  static const char PROGMEM LOG_TOPIC[];
  static const char PROGMEM LOG_MSG[];
public:
  static const char PROGMEM OK_STATE[];
  static const char PROGMEM ERR_STATE[];
  static const char PROGMEM DEVICE_TYPE[];
private:
  static const char PROGMEM INIT_PREFIX[];
  static const char PROGMEM FS_PREFIX[];
  static const char PROGMEM ETH_PREFIX[];
  static const char PROGMEM WIFI_PREFIX[];
  static const char PROGMEM NTP_PREFIX[];
  static const char PROGMEM JSON_PREFIX[];
  static const char PROGMEM TCP_PREFIX[];
  static const char PROGMEM MQTT_PREFIX[];

public:
  class WdtWrapper {
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
    uint8_t enabledResetNumber;
  };
  WdtWrapper WdtHandler;

  class DebugLED {
  public:
    DebugLED(uint8_t ledPin = 255, bool ledOnState = HIGH);
    virtual ~DebugLED() = default;
    inline void ledOn() __attribute__((always_inline));
    inline void ledOff() __attribute__((always_inline));
    void startTicker(uint32_t tickInterval_ms);
    void stopTicker();

    DebugLED(const DebugLED&) = delete;                       // Define copy constructor.
    DebugLED& operator=(const DebugLED&) = delete;            // Define copy assignment operator.
    DebugLED(DebugLED&&) = delete;                            // Define move constructor.
    DebugLED& operator=(DebugLED&&) = delete;                 // Define move assignment operator.

  private:
    inline IRAM_ATTR void ledToggle() __attribute__((always_inline));
    inline void ledHigh() __attribute__((always_inline));
    inline void ledLow() __attribute__((always_inline));

    const uint8_t ledPin_;
    const bool ledOnState_;
    Ticker ledTicker;
  };
  DebugLED debugLed;

  class Crc32 {
  public:
    Crc32();
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
    static constexpr uint32_t polynomial = 0xEDB88320; // CRC32 polynomial
    uint32_t crc;
  };

  /// @brief Base64 encoding and decoding of strings. Uses '+' for 62, '/' for 63, '=' for padding.
  class Base64 {
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
    static inline void fromA3ToA4(uint8_t* A4, uint8_t* A3);
    static inline void fromA4ToA3(uint8_t* A3, uint8_t* A4);
    static inline uint8_t lookupTable(char c);

  public:
    Base64(const Base64&) = delete;                       // Define copy constructor.
    Base64& operator=(const Base64&) = delete;            // Define copy assignment operator.
    Base64(Base64&&) = delete;                            // Define move constructor.
    Base64& operator=(Base64&&) = delete;                 // Define move assignment operator.

  private:
    static const char PROGMEM _Base64AlphabetTable[];
  };

  class OTA {
  public:
    OTA(Stream* serial = nullptr);

    /// @brief Destructor of the object.
    virtual ~OTA() = default;

    bool begin(uint32_t fileSize, uint32_t fileCrc, const char* fileName = OTA_FW_LOCATION);

    bool store(uint32_t filePieceNumber, const uint8_t* fileData, uint16_t fileDataSize);

    bool checkValidity();

    OTA(const OTA&) = delete;                       // Define copy constructor.
    OTA& operator=(const OTA&) = delete;            // Define copy assignment operator.
    OTA(OTA&&) = delete;                            // Define move constructor.
    OTA& operator=(OTA&&) = delete;                 // Define move assignment operator.

  private:
    uint32_t fileSize_;
    uint32_t fileCrc_;
    Stream* serialPort;
    uint32_t nextFilePieceNumber_;
    uint32_t remainingFileSize_;
    const char* fileName_;

    static const char PROGMEM OTA_PREFIX[];
    static const char PROGMEM OTA_FW_LOCATION[];
  };

  class MqttComBase {
  public:
    enum class Response : uint8_t {
      NACK = 0,
      ACK,
    };
  protected:
    MqttComBase(const char* classID);
    virtual ~MqttComBase() = default;
    void messageSend(const char* payload) const;
    virtual bool sendResponse(Response resp, uint16_t cmd);
  public:
    virtual void messageReceived(uint8_t* payload, uint32_t length) = 0;
    virtual bool begin() = 0;
    virtual bool loop() = 0;
    const char* getClassId() const;
    static void setMqttSender(std::function<void(const char*, const char*)> senderFunction);
    static void setConState(bool state);

    MqttComBase(const MqttComBase&) = delete;                       // Define copy constructor.
    MqttComBase& operator=(const MqttComBase&) = delete;            // Define copy assignment operator.
    MqttComBase(MqttComBase&&) = delete;                            // Define move constructor.
    MqttComBase& operator=(MqttComBase&&) = delete;                 // Define move assignment operator.
  protected:
    static bool getConState();
  private:
    char classId[16];
    static std::function<void(const char*, const char*)> mqttSender;
    static bool isOnline;
  };

private:
  class Common : public Connectivity::MqttComBase {
  public:
    enum class Command : uint8_t {
      BLANK = 0,
      RESTART,
      OTA_START,
      OTA_DATA,
      OTA_END
    };

    Common(const char* classID, Stream* serial = nullptr);

    /// @brief Destructor of the object.
    virtual ~Common() = default;

    virtual void messageReceived(uint8_t* payload, uint32_t length) override;

    virtual bool begin() override;

    virtual bool loop() override;

    /// @brief Reset the MCU.
    void restartESP();

  public:
    Common(const Common&) = delete;                       // Define copy constructor.
    Common& operator=(const Common&) = delete;            // Define copy assignment operator.
    Common(Common&&) = delete;                            // Define move constructor.
    Common& operator=(Common&&) = delete;                 // Define move assignment operator.
  private:
    Stream* serialPort;
    OTA ota;

    static const char PROGMEM COMMON_PREFIX[];
  };
  Common common;
};

#endif