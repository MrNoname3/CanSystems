//--- Headers ---//
#include <Arduino.h>                                                /// Arduino libraries header.
#include "canHandler.hpp"                                           /// CAN handler library.
#include "rgbLedWrapper.hpp"                                        /// RGB LED driver wrapper.
#include "pushButtonHandler.hpp"                                    /// Pushbutton events library.
#include "taskRunner.hpp"                                           /// Task runner class.
#include "common.hpp"                                               /// Common definitions and functions.
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
void analogSetup();
void measureMaxLoopTime();

//--- Asserts ---//
static_assert(digitalPinToInterrupt(CAN_INT) != (NOT_AN_INTERRUPT), "CAN modul interrupt input pin is not interrupt capable!");
static_assert(digitalPinToInterrupt(DFP_BUSY) != (NOT_AN_INTERRUPT), "DFPlayer modul interrupt input pin is not interrupt capable!");

//--- Driver objects ---//
CanHandler canHandler(Serial, CAN_CS, CAN_INT, LED_PIN, FLASH_CS);
PushButtonHandler buttonHandler(canHandler, []() -> bool {return (analogRead(BUTTON_PIN) > 500);});
RgbLedWrapper rgbLed(RGB_LED_NUM, RGB_PIN);
AmbientSensor ambientSensor(Serial, canHandler, LDR_PIN, Time::minToMs(15U));
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
  analogSetup();
  delay(1U);
  Serial.println(F("\r\n********\r\nStarting..."));
  taskRunner[0]->init();                                                      // Initialize CAN handler.
  buttonHandler.addBtnCallback(btnEventHandling);
  rgbLed.begin();
  extSensor.on();
  for(uint8_t i = 1U; i < taskNum; ++i) { taskRunner[i]->init(); }            // Call begin() on each object.
  Serial.println(F("********\r\nLooping..."));
  canHandler.ledOff();
}

void loop() {
  taskRunner[0]->run();                                                       // Run the CAN handler task in every loop.
  static uint8_t currentTask = 1U;                                            // Start from task 1.
  taskRunner[currentTask]->run();                                             // Run tasks in round-robin manner.
  currentTask = (currentTask % (taskNum - 1U)) + 1U;                          // Iterate over tasks from 1 to taskNum - 1.
  //measureMaxLoopTime();
}

void canMessageArrived(uint16_t command, const uint8_t (&data)[8]) {
  switch(command) {
    case static_cast<uint16_t>(CanCmd::RGB_LED): {
      rgbLed.setColor(data[0], data[1], data[2], true);
      canHandler.send(static_cast<uint16_t>(CanCmd::RGB_LED));
    } break;
    case static_cast<uint16_t>(CanCmd::PLAY_MP3): {
      const uint16_t songNum{static_cast<uint16_t>(data[0] | (data[1] << 8U))};
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

void analogSetup() {
  analogReference(DEFAULT);                                                   // Setup analog reference to 5V.
  bitSet(ADCSRA, ADPS2);                                                      // Fast ADC, set prescaler to 16.
  bitSet(ADCSRA, ADPS1);
  bitClear(ADCSRA, ADPS0);
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