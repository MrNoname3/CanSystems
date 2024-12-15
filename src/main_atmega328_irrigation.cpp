//--- Headers ---//
#include <Arduino.h>                                                /// Arduino libraries header.
#include "wdtHandler.hpp"                                           /// Handles the watchdog timer.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include "debugLedHandler.hpp"                                      /// Handles the debug LED.
#include "canHandler.hpp"                                           /// CAN handler library.
#include "rgbLedWrapper.hpp"                                        /// RGB LED driver wrapper.
#include "pushButtonHandler.hpp"                                    /// Pushbutton events library.
#include "taskHandler.hpp"                                          /// Class for task scheduling.
#include "common.hpp"                                               /// Common definitions and functions.
#include "performance.hpp"                                          /// Performance measurement class.
#include "pcf8574.hpp"                                              /// I2C GPIO expander.
#include "pumpControl.hpp"                                          /// Pump control class.
#include "multiplexer.hpp"                                          /// Analog multiplexer class.
#include "moistureReader.hpp"                                       /// Moisture sensor reader class.

//--- Constants ---//
static constexpr uint8_t RGB_LED_NUM                = 1U;           // Number of RGB LED's.
static constexpr uint8_t RGB_PIN                    = 7U;           // LED DATA PIN
static constexpr uint8_t LED_PIN                    = 4U;           // Pin of the LED.
static constexpr uint8_t CAN_CS                     = 10U;          // CS pin of the SPI CAN controller.
static constexpr uint8_t CAN_INT                    = 2U;           // Interrupt pin of the SPI CAN controller.
static constexpr uint8_t FLASH_CS                   = 8U;           // SPI FLASH CS pin.
static constexpr uint8_t BUTTON_PIN                 = 6U;           // Pushbutton pin.
static constexpr uint8_t FLOW_INT                   = 3U;           // Interrupt pin of the flow sensor.
static constexpr uint8_t PUMP_PWM                   = 5U;           // PWM pin of the pump.
static constexpr uint8_t ANALOG_EN                  = 9U;           // Analog power and multiplexer IC enable pin.
static constexpr uint8_t ANALOG_CHS[4]        = {A0, A1, A2, A3};   // Analog multiplexer channel select pins.
static constexpr uint8_t MOISTURE_SENSOR            = A6;           // Analog pin for moisture sensor.
static constexpr uint8_t CURRENT_SENSOR             = A7;           // Analog pin for current sensor.
static constexpr uint8_t MOISTURE_CH[8] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}; // Moisture sensor channel numbers.
static constexpr uint8_t MOISTURE_CH_NUM = sizeof(MOISTURE_CH) / sizeof(*MOISTURE_CH);  // Number of moisture sensors.

//--- Functions ---//
void canMessageArrived(uint16_t command, const uint8_t (&data)[8]);
void btnEventHandling(PushButtonHandler::BtnEvent btnEvent);
void maxLoopTimeCallback(uint32_t maxLoopTime);

//--- Asserts ---//
static_assert(digitalPinToInterrupt(CAN_INT) != (NOT_AN_INTERRUPT), "CAN modul interrupt input pin is not interrupt capable!");
static_assert(digitalPinToInterrupt(FLOW_INT) != (NOT_AN_INTERRUPT), "Flow sensor interrupt input pin is not interrupt capable!");

//--- Driver objects ---//
WdtHandler wdt(WdtHandler::WDT::T_120MS);
DebugLedHandler debugLed(LED_PIN, HIGH);
CanHandler canHandler(Serial, debugLed, CAN_CS, CAN_INT, FLASH_CS);
PushButtonHandler buttonHandler(canHandler, []() -> bool {return static_cast<bool>(digitalRead(BUTTON_PIN));});
RgbLedWrapper rgbLed(RGB_LED_NUM, RGB_PIN);
PCF8574 pcf(Time::msToUs(5U), 0x27);
PumpControl pc(
  pcf,
  rgbLed,
  PUMP_PWM,
  FLOW_INT,
  CURRENT_SENSOR,
  [](uint8_t errCode) -> void {
    canHandler.send(CanCmd::IRRIGATION_ERROR, {0U, 0U, 0U, 0U, 0U, 0U, 0U, errCode});
  }
);
Multiplexer analogMultiplexer(MOISTURE_SENSOR, ANALOG_EN, ANALOG_CHS);
MoistureReader<MOISTURE_CH_NUM> moistureReader(
  analogMultiplexer,
  rgbLed,
  MOISTURE_CH,
  Time::hrToMs(8U),
  [](const uint8_t (&data)[8]) -> void {
    canHandler.send(CanCmd::MOISTURE_DATA, data);
  }
);
Performance performance(2U, maxLoopTimeCallback);

//--- Handling tasks ---//
Task *task[6] = {&canHandler, &buttonHandler, &pcf, &pc, &moistureReader, &performance};
static constexpr uint8_t taskNum = sizeof(task) / sizeof(*task);
TaskHandler<taskNum, false> taskHandler(task);

//--- Setup section ---//
void setup() {
  wdt.resetWatchdog();
  Serial.begin(MONITOR_BAUD);
  debugLed.ledOn();
  canHandler.addCanCallback(canMessageArrived);
  Analog::config();
  delay(1U);
  Serial.println(F("\r\n********\r\nStarting..."));
  rgbLed.begin();
  buttonHandler.addBtnCallback(btnEventHandling);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  const uint32_t initResult = taskHandler.initTasks();
  const bool initSuccess = (initResult == 0U);
  Serial.print(F("Init: "));
  Serial.println(Str::getStateStr(initSuccess));
  if(!initSuccess) {
    Serial.print(F("Code: "));
    Serial.println(initResult, BIN);
    ResetHandler::restartMCU();
  }

  pc.addSafetyIrrigation(20U, 0U, 1U, false, false, 125U, 0U);
  pc.addSafetyIrrigation(Time::hrToMin(25U), 1U, 2U, false, false, 80U, 0U);

  Serial.println(F("********\r\nLooping..."));
  debugLed.ledOff();
}

void loop() {
  wdt.resetWatchdog();
  taskHandler.runTasks();
}

void canMessageArrived(uint16_t command, const uint8_t (&data)[8]) {
  switch(command) {
    case static_cast<uint16_t>(CanCmd::ADD_IRRIGATION): {
      pc.createIrrigation(data[0], data[1], data[2]);
      canHandler.send(command);
    } break;
    case static_cast<uint16_t>(CanCmd::SKIP_IRRIGATION): {
      pc.skipActualIrrigation();
      canHandler.send(command);
    } break;
    case static_cast<uint16_t>(CanCmd::STOP_IRRIGATION): {
      pc.skipAllIrrigations();
      canHandler.send(command);
    } break;
    case static_cast<uint16_t>(CanCmd::MOISTURE_DATA): {
      moistureReader.triggerImmediateMeasurement();
      canHandler.send(command);
    } break;
  }
}

void btnEventHandling(PushButtonHandler::BtnEvent btnEvent) {
  Serial.print(F("Btn: "));
  Serial.println(static_cast<uint8_t>(btnEvent));
  switch(btnEvent) {
    case PushButtonHandler::BtnEvent::LONG_PRESS: {
      pc.skipAllIrrigations();
    } break;
    case PushButtonHandler::BtnEvent::ONE_PRESS: {
      pc.skipActualIrrigation();
    } break;
    case PushButtonHandler::BtnEvent::TWO_PRESS: {
      moistureReader.triggerImmediateMeasurement();
    } break;
    default: {} break;
  }
}

void maxLoopTimeCallback(uint32_t maxLoopTime) {
  Serial.print(F("Max loop time: "));
  Serial.println(maxLoopTime);
  canHandler.send(CanCmd::LOOP_TIME_MAX, {
    static_cast<uint8_t>((maxLoopTime >> 0U) & 0xFF),
    static_cast<uint8_t>((maxLoopTime >> 8U) & 0xFF),
    static_cast<uint8_t>((maxLoopTime >> 16U) & 0xFF),
    static_cast<uint8_t>((maxLoopTime >> 24U) & 0xFF),
    0U, 0U, 0U, 0U
  });
}