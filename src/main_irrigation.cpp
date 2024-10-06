//--- Headers ---//
#include <Arduino.h>                                                /// Arduino libraries header.
#include "canHandler/src/canHandler.hpp"                            /// CAN handler library.
#include "rgbLedWrapper/src/rgbLedWrapper.hpp"                      /// RGB LED driver wrapper.
#include "pushButtonHandler/src/pushButtonHandler.hpp"              /// Pushbutton events library.

//--- Constants ---//
static constexpr uint8_t RGB_LED_NUM                = 1U;           // Number of RGB LED's.
static constexpr uint8_t RGB_PIN                    = 7U;           // LED DATA PIN
static constexpr uint8_t LED_PIN                    = 4U;           // Pin of the LED.
static constexpr uint8_t CAN_CS                     = 10U;          // CS pin of the SPI CAN controller.
static constexpr uint8_t CAN_INT                    = 2U;           // Interrupt pin of the SPI CAN controller.
static constexpr uint8_t FLASH_CS                   = 8U;           // SPI FLASH CS pin.
static constexpr uint8_t BUTTON_PIN                 = 8U;           // Pushbutton pin.
static constexpr uint8_t PUMP_0                     = 9U;

//--- Functions ---//
void canMessageArrived(uint16_t command, const uint8_t (&data)[8]);
void btnEventHandling(PushButtonHandler::BtnEvent btnEvent);
void measureMaxLoopTime();

//--- Driver objects ---//
CanHandler canHandler(Serial, CAN_CS, CAN_INT, LED_PIN, FLASH_CS);
PushButtonHandler buttonHandler(Serial, canHandler, BUTTON_PIN);
RgbLedWrapper rgbLed(RGB_LED_NUM, RGB_PIN);

//--- Array of function pointers ---//
void (*methodCallers[])() = {
  []() { buttonHandler.loop(); }
};
static constexpr uint8_t numMethods = sizeof(methodCallers) / sizeof(*methodCallers);

//--- Setup section ---//
void setup() {
  Serial.begin(MONITOR_BAUD);                                                 // Open serial port with the given baudrate.
  canHandler.ledOn();
  canHandler.addCanCallback(canMessageArrived);
  delay(1U);
  Serial.println(F("\r\n********\r\nStarting..."));
  canHandler.begin(500E3);                                                    // Set CAN speed to 500Kb/s.
  buttonHandler.addBtnCallback(btnEventHandling);
  rgbLed.begin();
  Serial.println(F("********\r\nLooping..."));
  canHandler.ledOff();
  pinMode(PUMP_0, OUTPUT);
}

enum class IrrigationState : uint8_t {
    IDLE = 0,
    SPINUP,
    RUNNING,
    SPINDOWN,
    STOPPED,
    ERROR
};
IrrigationState irrigationState = IrrigationState::IDLE;

void loop() {
  canHandler.loop();
  static uint8_t methodIndex = 0U;
  methodCallers[methodIndex++]();
  if(methodIndex >= numMethods) { methodIndex = 0U; }
  //measureMaxLoopTime();
  //const uint8_t pwmValue = map(analogRead(A0), 0U, 1023U, 0, 255U);
  //analogWrite(PUMP_0, pwmValue);
  //Serial.println(pwmValue);
  static bool irrigation = true;
  static uint32_t spinUpTimer = 0U;
  const uint32_t spinUpTime = 100U;
  static uint8_t pwmValue = 0U;
  static uint32_t runningTimer = 0U;
  const uint32_t runningTime = 40000U;
  static uint32_t spinDownTimer = 0U;
  const uint32_t spinDownTime = 300U;
  static uint32_t irrigationTimer = 0U;
  const uint32_t irrigationTime = 1800000U;

  if(millis() - irrigationTimer > irrigationTime) {
    irrigation = true;
    irrigationTimer = millis();
  }

  switch(irrigationState) {
    case IrrigationState::IDLE: {
      if(irrigation == true) {
        Serial.println("Irrigation");
        //pwmValue = 190U;
        pwmValue = 255U;
        spinUpTimer = millis();
        irrigationState = IrrigationState::SPINUP;
      }
    } break;
    case IrrigationState::SPINUP: {
      if(millis() - spinUpTimer >= spinUpTime) {
        pwmValue -= 10U;
        spinUpTimer = millis();
        if(pwmValue <= 150U) {
          runningTimer = millis();
          irrigationState = IrrigationState::RUNNING;
        }
      }
    } break;
    case IrrigationState::RUNNING: {
      if(millis() - runningTimer >= runningTime) {
        spinDownTimer = millis();
        irrigationState = IrrigationState::SPINDOWN;
      }
    } break;
    case IrrigationState::SPINDOWN: {
      if(millis() - spinDownTimer >= spinDownTime)  {
        spinDownTimer = millis();
        pwmValue -= 5U;
        if(pwmValue <= 50U) {
          irrigationState = IrrigationState::STOPPED;
        }
      }

    } break;
    case IrrigationState::STOPPED:
    case IrrigationState::ERROR: {
      pwmValue = 0U;
      irrigationState = IrrigationState::IDLE;
      irrigation = false;
    } break;
  }
  analogWrite(PUMP_0, pwmValue);
  

}

void canMessageArrived(uint16_t command, const uint8_t (&data)[8]) {
  
}

void btnEventHandling(PushButtonHandler::BtnEvent btnEvent) {
  switch(btnEvent) {
    case PushButtonHandler::BtnEvent::LONG_PRESS: {
      //static bool rgbLedState = false;
      //rgbLedState = !rgbLedState;
      //rgbLedState ? rgbLed.setColor(50U, 50U, 50U, true) : rgbLed.clear();
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