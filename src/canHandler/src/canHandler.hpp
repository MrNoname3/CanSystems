#ifdef PROJECT_CAN
#ifndef CAN_HANDLER_HPP
#define CAN_HANDLER_HPP

#include <Arduino.h>                          /// Arduino libraries header.
#include <HardwareSerial.h>
#include <vector>
#include "../../connectivity.hpp"
#include <LittleFS.h>                         /// Use FLASH filesystem.

class CanHandler final {
public:
  class CanComBase;

  struct __attribute__((packed))
  CanFrame {                                    // CAN frame.
    union {
      uint32_t extId;                           // Extended CAN ID.
      struct {
        uint32_t to : 10;                       // 10 bits for receiver address.
        uint32_t cmd : 9;                       // 9 bits for command.
        uint32_t from : 10;                     // 10 bits for sender address.
        uint32_t padding : 3;                   // Padding to fill up to 32 bits.
      };
    };
    uint8_t data[8];                            // CAN data.
    CanFrame() : extId(), data{0} {}
  };

  CanHandler(HardwareSerial& serial);
  /// @brief Destructor of the object.
  virtual ~CanHandler() = default;
  bool begin(uint32_t canBaud);
  bool loop();

  CanHandler(const CanHandler&) = delete;                       // Define copy constructor.
  CanHandler& operator=(const CanHandler&) = delete;            // Define copy assignment operator.
  CanHandler(CanHandler&&) = delete;                            // Define move constructor.
  CanHandler& operator=(CanHandler&&) = delete;                 // Define move assignment operator.
private:
  bool send(const CanFrame& frameOut);
  static void rxInterrupt(int packetsNum) __attribute__((optimize("-O3")));
  bool registerCallback(CanHandler::CanComBase* obj);

  HardwareSerial& serialPort;
  static constexpr uint8_t canRxQueueSize = 10U;
  static constexpr uint8_t canTxQueueSize = 10U;
  static QueueHandle_t canRxQueue;              // Queue handler for CAN RX.
  QueueHandle_t canTxQueue;                     // Queue handler for CAN TX.
  void* operator new(size_t size);              // Disable new operator.
  std::vector<CanHandler::CanComBase*> canDevices;
  static const char PROGMEM OK_STATE[];
  static const char PROGMEM ERR_STATE[];
  static const char PROGMEM CAN_PREFIX[];

public:
  class SoftwareTimer {
  public:
    SoftwareTimer(uint32_t time);
    /// @brief Destructor of the object.
    virtual ~SoftwareTimer() = default;
    bool isExpired();
    void reload();

    SoftwareTimer(const SoftwareTimer&) = delete;                       // Define copy constructor.
    SoftwareTimer& operator=(const SoftwareTimer&) = delete;            // Define copy assignment operator.
    SoftwareTimer(SoftwareTimer&&) = delete;                            // Define move constructor.
    SoftwareTimer& operator=(SoftwareTimer&&) = delete;                 // Define move assignment operator.
  private:
    const uint32_t time_;
    uint32_t start_time_;
  };

public:
  class Crc16 final {
  public:
    explicit Crc16(uint16_t initValue = 0xFFFF, uint16_t polynomial = 0x1021);
    ~Crc16() = default;
    void next(uint8_t value);
    void next(const uint8_t* values, uint32_t length);
    uint16_t get() const;
    static uint16_t calculate(const uint8_t *data, uint32_t length);

    Crc16(const Crc16&) = delete;                       // Define copy constructor.
    Crc16& operator=(const Crc16&) = delete;            // Define copy assignment operator.
    Crc16(Crc16&&) = delete;                            // Define move constructor.
    Crc16& operator=(Crc16&&) = delete;                 // Define move assignment operator.
  private:
    uint16_t crc_;                                      // CRC16 starting value.
    const uint16_t polynomial_;                         // CRC16 polynomial.
  };

public:
  class CanFileTransfer {
  public:
    CanFileTransfer(const char* fileName);
    /// @brief Destructor of the object.
    virtual ~CanFileTransfer();
    bool getNextFrame(uint8_t (&dataFrame)[8]);

    CanFileTransfer(const CanFileTransfer&) = delete;                       // Define copy constructor.
    CanFileTransfer& operator=(const CanFileTransfer&) = delete;            // Define copy assignment operator.
    CanFileTransfer(CanFileTransfer&&) = delete;                            // Define move constructor.
    CanFileTransfer& operator=(CanFileTransfer&&) = delete;                 // Define move assignment operator.
  private:
    File receivedFile;
    bool firstFrame;
    uint8_t frameNumber;
    uint16_t storageNumber;
    char fileName[28];
    uint32_t fileSize;
    uint16_t fileCrc;
  };

public:
  class CanComBase : protected Connectivity::MqttComBase {
  public:
    friend class CanHandler;
    CanComBase(const CanComBase&) = delete;                       // Define copy constructor.
    CanComBase& operator=(const CanComBase&) = delete;            // Define copy assignment operator.
    CanComBase(CanComBase&&) = delete;                            // Define move constructor.
    CanComBase& operator=(CanComBase&&) = delete;                 // Define move assignment operator.
  protected:
    CanComBase(CanHandler& canHandler, uint32_t canId, Connectivity& connectivity, const char* classID);
    /// @brief Destructor of the object.
    virtual ~CanComBase() = default;
    /// @brief Base command list for nodes.
    enum class CanCmd : uint16_t {
      PING = 0,                              // Ping command.
      RESTART,                               // Node restart command.
      BUTTON_EVENT,                          // Button event occured.
      OTA_START,                             // Init OTA process.
      OTA_SEND,                              // Stream FW bytes to OTA handler.
      OTA_END                                // OTA process ended.
    };
    virtual bool init() = 0;
    virtual bool run() = 0;
    virtual void canFrameReceived(CanHandler::CanFrame& canFrame) = 0;
    void sendCanFrame(CanCmd command, const uint8_t (&data)[8]) const;
    void sendCanFrame(uint16_t command, const uint8_t (&data)[8]) const;
    void sendCanCmd(CanCmd command) const;
    void sendCanCmd(uint16_t command) const;
    bool sendResponse(Response resp, uint16_t cmd, const uint8_t (&data)[8]);
  private:
    bool beginPriv();
    bool loopPriv();
    void canFrameReceivedPriv(CanHandler::CanFrame& canFrame);
    const uint32_t getCanId() const;
    virtual bool begin() override;
    virtual bool loop() override;
    virtual void messageReceived(uint8_t* payload, uint32_t length) override;
    bool sendFilePiece(CanCmd command);
    static constexpr uint16_t localCanId = 10U;
    static constexpr uint32_t pingTime = 500U;
    static constexpr uint32_t alertTime = 1000U;
    CanHandler& canHandler;
    const uint32_t nodeCanId;
    SoftwareTimer pingTimer;
    SoftwareTimer alertTimer;
    bool nodeAlive_;
    CanFileTransfer* canFileTransfer;
    static const char PROGMEM CAN_BASE_PREFIX[];
  };

};
#endif // CAN_HANDLER_HPP
#endif // PROJECT_CAN