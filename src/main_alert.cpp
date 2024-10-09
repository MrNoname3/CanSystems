//--- Headers ---//
#include <Arduino.h>                                                /// Arduino libraries header.
#include "canHandler/src/canHandler.hpp"                            /// CAN handler library.
#include "rgbLedWrapper/src/rgbLedWrapper.hpp"                      /// RGB LED driver wrapper.
#include "pushButtonHandler/src/pushButtonHandler.hpp"              /// Pushbutton events library.
#include "dfPlayer/src/dfPlayer.hpp"                                /// MP3 player driver library.
#include "ambientSensor/src/ambientSensor.hpp"                      /// Sensor handelr library.
#include "externalSensor/src/externalSensor.hpp"                    /// External temperature and humidity sensor library.
#include "taskRunner/src/taskRunner.hpp"                            /// Task runner class.

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
void measureMaxLoopTime();

//--- Driver objects ---//
CanHandler canHandler(Serial, CAN_CS, CAN_INT, LED_PIN, FLASH_CS);
PushButtonHandler buttonHandler(Serial, canHandler, [](){return analogRead(BUTTON_PIN) > 500 ? true : false;});
RgbLedWrapper rgbLed(RGB_LED_NUM, RGB_PIN);
static constexpr uint32_t measureTimeMs = static_cast<uint32_t>(15UL * 60UL * 1000UL);
AmbientSensor ambientSensor(Serial, canHandler, LDR_PIN, measureTimeMs);
DFPlayer mp3Player(rgbLed, DFP_RX, DFP_TX, DFP_EN, DFP_BUSY);
const ExternalSensor extSensor(EXT_SENSOR_EN);

//--- Handling tasks ---//
TaskRunner *taskRunner[] = {&canHandler, &buttonHandler, &ambientSensor, &mp3Player};
static constexpr uint8_t taskNum = sizeof(taskRunner) / sizeof(*taskRunner);

//--- Setup section ---//
void setup() {
  Serial.begin(MONITOR_BAUD);                                                 // Open serial port with the given baudrate.
  canHandler.ledOn();
  canHandler.addCanCallback(canMessageArrived);
  delay(1U);
  Serial.println(F("\r\n********\r\nStarting..."));
  for(uint8_t i = 0; i < taskNum; ++i) { taskRunner[i]->init(); }             // Call begin() on each object.
  buttonHandler.addBtnCallback(btnEventHandling);
  rgbLed.begin();
  extSensor.on();
  Serial.println(F("********\r\nLooping..."));
  canHandler.ledOff();
}

void loop() {
  static uint8_t currentTask = 0U;
  taskRunner[currentTask]->run();
  currentTask = (currentTask + 1U) % taskNum;
  //measureMaxLoopTime();
}

void canMessageArrived(uint16_t command, const uint8_t (&data)[8]) {
  switch(command) {
    case static_cast<uint16_t>(CanCmd::RGB_LED): {
      rgbLed.setColor(data[0], data[1], data[2], true);
      canHandler.send(static_cast<uint16_t>(CanCmd::RGB_LED));
    } break;
    case static_cast<uint16_t>(CanCmd::PLAY_MP3): {
      const uint16_t songNum{static_cast<uint16_t>(data[0] | (data[1] << 8))};
      mp3Player.play(songNum, data[2], data[3], data[4], data[5]);
      canHandler.send(static_cast<uint16_t>(CanCmd::PLAY_MP3));
    } break;
  };
}

void btnEventHandling(PushButtonHandler::BtnEvent btnEvent) {
  switch(btnEvent) {
    case PushButtonHandler::BtnEvent::LONG_PRESS: {
      static bool rgbLedState = false;
      rgbLedState = !rgbLedState;
      rgbLedState ? rgbLed.setColor(50U, 50U, 50U, true) : rgbLed.clear();
    } break;
    default: {} break;
  }
}

void measureMaxLoopTime() {
  static uint32_t maxLoopTime = 1UL;
  static uint32_t lastLoopTime = millis();
  uint32_t actualLoopTime = millis() - lastLoopTime;
  lastLoopTime = millis();
  if(actualLoopTime > maxLoopTime) {
    maxLoopTime = actualLoopTime;
    Serial.print(F("Max loop time: "));
    Serial.println(maxLoopTime);
  }
}