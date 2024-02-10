#ifdef PROJECT_CAN
#ifndef CAN_HANDLER_HPP
#define CAN_HANDLER_HPP

#include <Arduino.h>                          /// Arduino libraries header.
#include <HardwareSerial.h>
#include <vector>

class CanHandler final {
public:
  class CanComBase;

  union __attribute__((packed))
  CanId {                                       // CAN ID store / convert.
    uint32_t id;                                // Extended CAN ID.
    struct {
      uint32_t to : 10;                         // 10 bits for receiver address.
      uint32_t cmd : 9;                         // 9 bits for command.
      uint32_t from : 10;                       // 10 bits for sender address.
      uint32_t padding : 3;                     // Padding to fill up to 32 bits.
    };
    CanId() : id{0U} {}
  };

  struct __attribute__((packed))
  CanFrame {                                    // CAN frame.
    CanId canId;                                // CAN ID.
    uint8_t data[8];                            // CAN data.
    CanFrame() : canId(), data{0} {}
  };

  CanHandler(HardwareSerial& serial);
  /// @brief Destructor of the object.
  virtual ~CanHandler() = default;
  bool begin(uint32_t canBaud);
  bool loop();

private:
  bool send(const CanFrame& frameOut);
  static void rxInterrupt(int packetsNum) __attribute__((optimize("-O3")));
  bool registerCallback(CanHandler::CanComBase* obj);

public:
  CanHandler(const CanHandler&) = delete;                       // Define copy constructor.
  CanHandler& operator=(const CanHandler&) = delete;            // Define copy assignment operator.
  CanHandler(CanHandler&&) = delete;                            // Define move constructor.
  CanHandler& operator=(CanHandler&&) = delete;                 // Define move assignment operator.
private:
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
  class CanComBase {
  public:
    friend class CanHandler;
    CanComBase(const CanComBase&) = delete;                       // Define copy constructor.
    CanComBase& operator=(const CanComBase&) = delete;            // Define copy assignment operator.
    CanComBase(CanComBase&&) = delete;                            // Define move constructor.
    CanComBase& operator=(CanComBase&&) = delete;                 // Define move assignment operator.
  protected:
    CanComBase(CanHandler& canHandler, uint32_t canId);
    /// @brief Destructor of the object.
    virtual ~CanComBase() = default;
    /// @brief Base command list for nodes.
    enum class CanCmd : uint16_t {
      IDLE = 0,                              // Idle state.
      PING,                                  // Ping command.
      RESET,                                 // Node reset command.
      GET_FW_VERSION,                        // Firmware version command.
      SETADDRESS,                            // CAN address setup.
      NODE_RESTARTED,                        // Node restarted.
      BUTTON_EVENT,                          // Button event occured.
      OTA_START,                             // Init OTA process.
      OTA_SEND,                              // Stream FW bytes to OTA handler.
      OTA_END,                               // OTA process ended.
      RGB_LED,                               // Set WS2812 RGB LED color.
    };
    virtual bool init() = 0;
    virtual bool run(bool nodeAlive) = 0;
    virtual void canFrameReceived(CanHandler::CanFrame& canFrame) = 0;
    void sendCanFrame(CanCmd command, const uint8_t (&data)[8]) const;
    void sendCanFrame(uint16_t command, const uint8_t (&data)[8]) const;
    void sendCanCmd(CanCmd command) const;
    void sendCanCmd(uint16_t command) const;
  private:
    bool beginPriv();
    bool loopPriv();
    void canFrameReceivedPriv(CanHandler::CanFrame& canFrame);
    const uint32_t getCanId() const;
    static constexpr uint16_t localCanId = 10U;
    static constexpr uint32_t pingTime = 500U;
    static constexpr uint32_t alertTime = 1000U;
    CanHandler& canHandler;
    const uint32_t nodeCanId;
    SoftwareTimer pingTimer;
    SoftwareTimer alertTimer;
  };

};
#endif // CAN_HANDLER_HPP
#endif // PROJECT_CAN