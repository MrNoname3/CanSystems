//--- Headers ---//
#include <Arduino.h>                                                /// Arduino libraries header.
#include "canHandler.hpp"                                           /// CAN handler library.
#include "rgbLedWrapper.hpp"                                        /// RGB LED driver wrapper.
#include "pushButtonHandler.hpp"                                    /// Pushbutton events library.
#include "taskRunner.hpp"                                           /// Task runner class.
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
static constexpr uint8_t MOISTURE_CH_NUM = sizeof(MOISTURE_CH) / sizeof(*MOISTURE_CH); // Number of moisture sensors.

//--- Functions ---//
void canMessageArrived(uint16_t command, const uint8_t (&data)[8]);
void btnEventHandling(PushButtonHandler::BtnEvent btnEvent);
void analogSetup();
void measureMaxLoopTime();

//--- Asserts ---//
static_assert(digitalPinToInterrupt(CAN_INT) != (NOT_AN_INTERRUPT), "CAN modul interrupt input pin is not interrupt capable!");
static_assert(digitalPinToInterrupt(FLOW_INT) != (NOT_AN_INTERRUPT), "Flow sensor interrupt input pin is not interrupt capable!");

//--- Driver objects ---//
CanHandler canHandler(Serial, CAN_CS, CAN_INT, LED_PIN, FLASH_CS);
PushButtonHandler buttonHandler(Serial, canHandler, [](){return static_cast<bool>(digitalRead(BUTTON_PIN));});
RgbLedWrapper rgbLed(RGB_LED_NUM, RGB_PIN);
PCF8574 pcf(0x27);
PumpControl pc(pcf, PUMP_PWM, FLOW_INT, CURRENT_SENSOR, [](uint8_t errCode){canHandler.send(CanCmd::IRRIGATION_ERROR, {0U, 0U, 0U, 0U, 0U, 0U, 0U, errCode});});
Multiplexer analogMultiplexer(MOISTURE_SENSOR, ANALOG_EN, ANALOG_CHS);
MoistureReader<MOISTURE_CH_NUM> moistureReader(analogMultiplexer, MOISTURE_CH, 5U, [](const uint8_t (&data)[8]){canHandler.send(CanCmd::MOISTURE_DATA, data);});

//--- Handling tasks ---//
TaskRunner *taskRunner[] = {&canHandler, &buttonHandler, &pc, &moistureReader};
static constexpr uint8_t taskNum = sizeof(taskRunner) / sizeof(*taskRunner);

//--- Setup section ---//
void setup() {
  Serial.begin(MONITOR_BAUD);                                                 // Open serial port with the given baudrate.
  canHandler.ledOn();
  canHandler.addCanCallback(canMessageArrived);
  analogSetup();
  delay(1U);
  Serial.println(F("\r\n********\r\nStarting..."));
  pinMode(BUTTON_PIN, INPUT_PULLUP);                                          // Set button pin as input with pullup resistor.
  taskRunner[0]->init();                                                      // Initialize CAN handler.
  buttonHandler.addBtnCallback(btnEventHandling);
  rgbLed.begin();
  Serial.print(F("PCF8574: "));
  const bool pcfAvailable = pcf.begin();
  Serial.println(pcfAvailable ? CanHandler::OK_STATE : CanHandler::ERR_STATE);  // Check if PCF8574 is available.
  if(!pcfAvailable) { canHandler.restartMCU(); }                              // If not, restart MCU.
  for(uint8_t i = 1; i < taskNum; ++i) { taskRunner[i]->init(); }             // Call begin() on each object.
  Serial.println(F("********\r\nLooping..."));
  canHandler.ledOff();
}

void loop() {
  taskRunner[0]->run();                                                       // Run the CAN handler task in every loop.
  static uint8_t currentTask = 1U;                                            // Start from task 1.
  taskRunner[currentTask]->run();                                             // Run tasks in round-robin manner.
  currentTask = (currentTask % (taskNum - 1U)) + 1U;                          // Iterate over tasks from 1 to taskNum - 1.
  // measureMaxLoopTime();
}

void canMessageArrived(uint16_t command, const uint8_t (&data)[8]) {
  switch(command) {
    case static_cast<uint16_t>(CanCmd::IRRIGATION): {
      pc.createIrrigation(data[0], data[1], data[2]);
      canHandler.send(static_cast<uint16_t>(CanCmd::IRRIGATION));
    } break;
  }
}

void btnEventHandling(PushButtonHandler::BtnEvent btnEvent) {
  switch(btnEvent) {
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