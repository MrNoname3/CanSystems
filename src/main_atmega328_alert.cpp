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
#include "dfPlayer.hpp"                                             /// MP3 player driver library.
#include "ambientSensor.hpp"                                        /// Sensor handelr library.
#include "externalSensor.hpp"                                       /// External temperature and humidity sensor library.

//--- Constants ---//
static constexpr uint8_t RGB_LED_NUM                = 19U;          // Number of RGB LED's.
static constexpr uint8_t RGB_PIN                    = 7U;           // LED DATA PIN
static constexpr uint8_t LED_PIN                    = 4U;           // Pin of the LED.
static constexpr uint8_t CAN_CS                     = 10U;          // CS pin of the SPI CAN controller.
static constexpr uint8_t CAN_INT                    = 2U;           // Interrupt pin of the SPI CAN controller.
static constexpr uint8_t FLASH_CS                   = 8U;           // SPI FLASH CS pin.
static constexpr uint8_t BUTTON_PIN                 = 20U;          // Pushbutton pin. (A6)
static constexpr uint8_t DFP_EN                     = 9U;           // DFPlayer switch pin.
static constexpr uint8_t DFP_BUSY                   = 3U;           // DFPlayer busy pin.
static constexpr uint8_t DFP_TX                     = 5U;           // DFPlayer serial RX pin.
static constexpr uint8_t DFP_RX                     = 6U;           // DFPlayer serial TX pin.
static constexpr uint8_t LDR_PIN                    = 21U;          // Analog light sensor pin. (A7)
static constexpr uint8_t EXT_SENSOR_EN              = 17U;          // External sensor enable pin. (A3)
static constexpr uint8_t RS232_RX                   = 15U;          // RS232 serial RX pin. (A2)
static constexpr uint8_t RS232_TX                   = 16U;          // RS232 serial TX pin. (A1)

//--- Functions ---//
void canMessageArrived(uint16_t command, const uint8_t (&data)[8]);
void btnEventHandling(PushButtonHandler::BtnEvent btnEvent);
void maxLoopTimeCallback(uint32_t maxLoopTime);

//--- Asserts ---//
static_assert(digitalPinToInterrupt(CAN_INT) != (NOT_AN_INTERRUPT), "CAN modul interrupt input pin is not interrupt capable!");
static_assert(digitalPinToInterrupt(DFP_BUSY) != (NOT_AN_INTERRUPT), "DFPlayer modul interrupt input pin is not interrupt capable!");

//--- Driver objects ---//
WdtHandler wdt(WdtHandler::WDT::T_1S);
DebugLedHandler debugLed(LED_PIN, HIGH);
CanHandler canHandler(debugLed, CAN_CS, CAN_INT, FLASH_CS);
PushButtonHandler buttonHandler(canHandler, []() -> bool {return (analogRead(BUTTON_PIN) > 500);});
RgbLedWrapper rgbLed(RGB_LED_NUM, RGB_PIN);
AmbientSensor ambientSensor(canHandler, LDR_PIN, Time::minToMs(15U));
DFPlayer mp3Player(rgbLed, DFP_RX, DFP_TX, DFP_EN, DFP_BUSY);
const ExternalSensor extSensor(EXT_SENSOR_EN);
Performance performance(3U, maxLoopTimeCallback);

//--- Handling tasks ---//
Task *task[5] = {&canHandler, &buttonHandler, &ambientSensor, &mp3Player, &performance};
static constexpr uint8_t taskNum = arraySize(task);
TaskHandler<taskNum, false> taskHandler(task);

//--- Setup section ---//
void setup() {
  wdt.resetWatchdog();
  Serial.begin(MONITOR_BAUD);
  DebugLedHandler::ledOn();
  canHandler.addCanCallback(canMessageArrived);
  Analog::config();
  delay(1U);
  Logger::get().println(F("\r\n********\r\nStarting..."));
  Build::printBuildInfo();
  rgbLed.begin();
  buttonHandler.addBtnCallback(btnEventHandling);

  extSensor.on();

  const uint32_t initResult = taskHandler.initTasks();
  const bool initSuccess = (initResult == 0U);
  Logger::get().print(F("Init: "));
  Logger::get().println(Str::getStateStr(initSuccess));
  if(!initSuccess) {
    Logger::get().print(F("Code: "));
    Logger::get().println(initResult, BIN);
    ResetHandler::restartMCU();
  }

  Logger::get().println(F("********\r\nLooping..."));
  DebugLedHandler::ledOff();
}

void loop() {
  wdt.resetWatchdog();
  (void)taskHandler.runTasks();
}

void canMessageArrived(uint16_t command, const uint8_t (&data)[8]) {
  switch(command) {
    case static_cast<uint16_t>(CanCmd::RGB_LED): {
      rgbLed.setColor(data[0], data[1], data[2], true);
      canHandler.send(command);
    } break;
    case static_cast<uint16_t>(CanCmd::PLAY_MP3): {
      const uint16_t songNum{static_cast<uint16_t>(data[0] | (data[1] << 8U))};
      mp3Player.play(songNum, data[2], data[3], data[4], data[5]);
      canHandler.send(command);
    } break;
  };
}

void btnEventHandling(PushButtonHandler::BtnEvent btnEvent) {
  Logger::get().print(F("Btn: "));
  Logger::get().println(static_cast<uint8_t>(btnEvent));
  switch(btnEvent) {
    case PushButtonHandler::BtnEvent::LONG_PRESS: {
      static bool rgbLedState = false;
      rgbLedState = !rgbLedState;
      rgbLedState ? rgbLed.setColor(50U, 50U, 50U, true) : rgbLed.clear();
    } break;
    default: {} break;
  }
}

void maxLoopTimeCallback(uint32_t maxLoopTime) {
  Logger::get().print(F("Max loop time: "));
  Logger::get().println(maxLoopTime);
  canHandler.send(CanCmd::LOOP_TIME_MAX, {
    static_cast<uint8_t>((maxLoopTime >> 0U) & 0xFF),
    static_cast<uint8_t>((maxLoopTime >> 8U) & 0xFF),
    static_cast<uint8_t>((maxLoopTime >> 16U) & 0xFF),
    static_cast<uint8_t>((maxLoopTime >> 24U) & 0xFF),
    0U, 0U, 0U, 0U
  });
}