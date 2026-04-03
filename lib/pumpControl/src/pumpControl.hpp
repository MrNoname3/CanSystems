#ifndef PUMP_CONTROL_HPP
#define PUMP_CONTROL_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include "taskHandler.hpp"                                          /// Class for task scheduling.
#include "pcf8574.hpp"                                              /// I2C GPIO expander.
#include "CircularBuffer.hpp"                                       /// Circular buffer class.
#include "rgbLedWrapper.hpp"                                        /// RGB LED driver wrapper.
#include "common.hpp"                                               /// Common definitions and functions.

/// @class PumpControl
/// @brief Controls irrigation pumps, monitors flow rate, and manages irrigation schedules and safety limits.
class PumpControl final : public Task {
private:
  using PumpControlErrorType = uint8_t;                             // Underlying type for pump control error states.

public:
  /// @brief Constructor for the PumpControl class.
  /// @param pcf8574 Reference to the I2C GPIO expander.
  /// @param rgbLed Reference to RGB LED driver object.
  /// @param pwmPin PWM pin for pump control.
  /// @param intPin Interrupt pin for flow sensor.
  /// @param currentSensePin Pin to sense the current.
  /// @param reportError Callback function to report error codes.
  PumpControl(PCF8574& pcf8574, RgbLedWrapper& rgbLed, uint8_t pwmPin, uint8_t intPin, uint8_t currentSensePin, void (*reportError)(uint8_t errCode));

  /// @brief Default destructor.
  ~PumpControl() override = default;

  /// @brief Initializes the pump control system.
  /// @return `true`.
  bool init() override;

  /// @brief Runs the main irrigation control loop.
  /// @return `true`.
  bool run() override;

  /// @brief Creates a new irrigation schedule.
  /// @param irrigationInfo Information on the irrigation task.
  /// @param pwmValue PWM value to run the pump.
  /// @param repeatNum Number of repetitions.
  void createIrrigation(uint8_t irrigationInfo, uint8_t pwmValue, uint8_t repeatNum);

  /// @brief Creates a new irrigation schedule with additional options.
  /// @param channel Channel number to use. Value: 0-3.
  /// @param duration Duration of irrigation in minutes. Value: 0-7.
  /// @param checkFlow Whether to check the flow sensor.
  /// @param checkCurrent Whether to check current consumption.
  /// @param pwmValue PWM value to run the pump. Value: 0-255.
  /// @param repeatNum Number of repetitions. Value: 0-255.
  void createIrrigation(uint8_t channel, uint8_t duration, bool checkFlow, bool checkCurrent, uint8_t pwmValue, uint8_t repeatNum);

  /// @brief Calculates the consumed current of the pump.
  /// @return The current value in mA.
  [[nodiscard]] int16_t calculateCurrent() const;

  /// @brief Calculates only positive current and dont't care with negative ones.
  /// @return Returns the positive current value. In negative case it returns with 0.
  [[nodiscard]] uint16_t getPositiveCurrent() const;

  /// @brief Adds a limit switch for a specific channel.
  /// @param channel The irrigation channel.
  /// @param limitSwitch Function pointer to the limit switch.
  void addLimitSwitch(uint8_t channel, bool (*limitSwitch)());

  /// @brief Skips the currently active irrigation task.
  void skipActualIrrigation();

  /// @brief Skips all scheduled irrigation tasks.
  void skipAllIrrigations();

  /// @brief Adds a safety irrigation task with specified parameters.
  /// @param time Time of the safety irrigation in minutes.
  /// @param channel Channel number for the irrigation. Value: 0-3.
  /// @param duration Duration of irrigation in minutes. Value: 0-7.
  /// @param checkFlow Whether to check the flow sensor.
  /// @param checkCurrent Whether to check current consumption.
  /// @param pwmValue PWM value to run the pump. Value: 0-255.
  /// @param repeatNum Number of repetitions. Value: 0-255.
  void addSafetyIrrigation(uint16_t time, uint8_t channel, uint8_t duration, bool checkFlow, bool checkCurrent, uint8_t pwmValue, uint8_t repeatNum);

  PumpControl(const PumpControl&) = delete;               // Define copy constructor.
  PumpControl& operator=(const PumpControl&) = delete;    // Define copy assignment operator.
  PumpControl(PumpControl&&) = delete;                    // Define move constructor.
  PumpControl& operator=(PumpControl&&) = delete;         // Define move assignment operator.

private:
  /// @brief Structure to represent an irrigation queue element.
  struct __attribute__((packed))
  IrrigationQueueElement {
    union {
      uint8_t irrigationInfo;               // Encoded irrigation information.
      struct {
        uint8_t channel : 2;                // Channel for irrigation. Value: 0-3.
        uint8_t duration : 3;               // Duration of irrigation in minutes. Value: 0-7.
        uint8_t checkFlow : 1;              // Checking flow sensor flag.
        uint8_t checkCurrent : 1;           // Checking current sensor flag.
        uint8_t padding : 1;                // Unused padding bit.
      };
    };
    uint8_t pwmValue;                       // PWM value to run the pump. Value: 0-255.
    uint8_t repeatNum;                      // Number of repetitions. Value: 0-255.

    /// @brief Default constructor initializes all values to zero.
    IrrigationQueueElement() : irrigationInfo(0U), pwmValue(0U), repeatNum(0U) {}

    /// @brief Constructor initializing with encoded irrigation info, PWM, and repeat count.
    /// @param irrigationInfo Encoded byte with irrigation information.
    /// @param pwmValue PWM value for pump speed control.
    /// @param repeatNum Number of times to repeat the irrigation.
    IrrigationQueueElement(uint8_t irrigatinInfo, uint8_t pwmValue, uint8_t repeatNum) :
      irrigationInfo(irrigatinInfo),
      pwmValue(pwmValue),
      repeatNum(repeatNum)
    {}

    /// @brief Constructor initializing with individual irrigation parameters.
    /// @param channel Channel to irrigate. Value: (0-3).
    /// @param duration Duration in minutes. Value: (0-7).
    /// @param checkFlow True to enable flow sensor checking.
    /// @param checkCurrent True to enable current sensor checking.
    /// @param pwmValue PWM value for pump speed control.
    /// @param repeatNum Number of times to repeat the irrigation.
    IrrigationQueueElement(uint8_t channel, uint8_t duration, bool checkFlow, bool checkCurrent, uint8_t pwmValue, uint8_t repeatNum) :
      channel(channel &= channelSafetyMask),
      duration(duration & 7U),
      checkFlow(static_cast<uint8_t>(checkFlow)),
      checkCurrent(static_cast<uint8_t>(checkCurrent)),
      pwmValue(pwmValue),
      repeatNum(repeatNum)
    {}
  };

  /// @brief Structure to represent a safety irrigation element.
  struct __attribute__((packed))
  SafetyIrrigationElement {
    uint16_t time;                          // Time for safety irrigation in minutes.
    uint32_t timer;                         // Timer for safety monitoring.
    IrrigationQueueElement irrigation;      // Irrigation task details.

    /// @brief Default constructor initializes all values to zero.
    SafetyIrrigationElement() : time(0U), timer(0U) {}

    /// @brief Constructor initializing all members for a safety irrigation task.
    /// @param time Scheduled safety irrigation time in minutes.
    /// @param timer Timer value for countdown.
    /// @param irrigation Irrigation task details.
    SafetyIrrigationElement(uint32_t time, uint32_t timer, IrrigationQueueElement irrigation) :
      time(time),
      timer(timer),
      irrigation(irrigation)
    {}
  };

  /// @brief Represents different states in the irrigation control process.
  enum class IrrigationState : uint8_t {
    IDLE = 0,                               // Idle state, no active irrigation.
    RUN,                                    // Irrigation is currently active.
    STOP,                                   // Irrigation is stopped.
    ERROR,                                  // The system runs into an error.
    CALIBRATION                             // System is in calibration mode.
  };

  /// @brief Represents error states in the pump control system.
  enum class PumpControlError : PumpControlErrorType {
    NONE          = 0U,                     // No error.
    CH_SELECT     = 1 << 0U,                // Channel select error.
    FLOW_STUCK    = 1 << 1U,                // Flow meter stuck error.
    FLOW_OVERRUN  = 1 << 2U,                // Flow meter counts, when it should not.
    PUMP_OVERRUN  = 1 << 3U,                // Pump takes current, when it should not.
    PUMP_OC       = 1 << 4U,                // Pump overcurrent error.
    PUMP_UC       = 1 << 5U,                // Pump undercurrent error.
    QUEUE_FULL    = 1 << 6U                 // Irrigation queue is full.
  };

  void handleIdle(uint32_t actualTime);
  void handleRun(uint32_t actualTime);
  void handleStop();
  void handleError();
  void handleCalibration(uint32_t actualTime);

  /// @brief Interrupt handler to update flow count when flow sensor is triggered.
  static void irqHandler();

  /// @brief Selects an irrigation channel for use.
  /// @param channel The channel to select.
  /// @return True if the channel was successfully selected, false otherwise.
  [[nodiscard]] bool selectChannel(uint8_t channel) const;

  /// @brief Schedules an irrigation task.
  /// @param irrigationElement The irrigation task details.
  void createIrrigation(IrrigationQueueElement irrigationElement);

  /// @brief Checks if safety irrigation times have been exceeded.
  void checkSafetyIrrigations();

  /// @brief Resets the timer used for monitoring safety timers on a specific channel.
  /// @param channel The channel to reset the timer for.
  void resetSafetyIrrigationTimer(uint8_t channel);

  static constexpr uint8_t channelCount = 4U;                               // Number of irrigation channels.
  static constexpr uint8_t channelSafetyMask = channelCount - 1U;           // Channel mask to prevent memory overlapping.
  static constexpr uint16_t errorCheckTime = Time::secToMs(1U);             // Error check interval in ms.
  static constexpr uint8_t maxAllowedStandbyCurrent = 100U;                 // Maximum standby current in mA.
  static constexpr uint16_t maxAllowedCurrent = 1000U;                      // Maximum working current in mA.
  static constexpr uint8_t irrStartColors[3] = {0U, 5U, 10U};               // RGB LED colors when irrigation started.

  static volatile uint16_t flowCounter;                                     // Flow counter for water flow sensor measurement.
  PCF8574& pcf;                                                             // Reference to the GPIO expander.
  RgbLedWrapper& rgbLed;                                                    // Reference to RGB LED driver object.
  const uint8_t pwmPin;                                                     // PWM control pin.
  const uint8_t intPin;                                                     // Flow sensor interrupt pin.
  const uint8_t currentSensePin;                                            // Current sense sensor pin.
  uint16_t prevFlowCounter;                                                 // Previous flow counter value.
  CircularBuffer<IrrigationQueueElement, channelCount> irrigationQueue;     // Queue for pending irrigation tasks.
  IrrigationState irrigationState;                                          // Current state of the irrigation control.
  uint16_t analogValue;                                                     // Filtered analog value from sensor.
  uint32_t eventTimer;                                                      // Class wide variable for universal timings.
  uint32_t errorCheckTimer;                                                 // Timer variable for error checking.
  ErrorState<PumpControlError, PumpControlErrorType> pumpControlErrState;   // Error code handler object.
  void (*reportError)(uint8_t errCode);                                     // Reports an error state via a callback function if set.
  bool (*limitSwitches[channelCount])();                                    // Array of limit switches for safety stop.
  int16_t calibrationValue;                                                 // Calibration value for current sense sensor.
  SafetyIrrigationElement safetyIrrigation[channelCount];                   // Safety irrigation elements per channel.
};
#endif // PUMP_CONTROL_HPP