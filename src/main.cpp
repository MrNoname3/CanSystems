#include "main.hpp"

//--- Driver objects ---//
CanHandler canHandler(Serial, CAN_CS, CAN_INT, LED_PIN, FLASH_CS);
PushButtonHandler buttonHandler(Serial, canHandler, BUTTON_PIN);
RgbLedWrapper rgbLed(RGB_LED_NUM, RGB_PIN);
static constexpr uint32_t measureTimeMs = 1U * 60U * 1000U;
AmbientSensor ambientSensor(Serial, canHandler, LDR_PIN, measureTimeMs);
DFPlayer mp3Player(rgbLed, DFP_RX, DFP_TX, DFP_EN, DFP_BUSY);
const ExternalSensor extSensor(EXT_SENSOR_EN);

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
  ambientSensor.begin();
  extSensor.on();
  Serial.println(F("********\r\nLooping..."));
  canHandler.ledOff();
}

void loop() {
  canHandler.loop();
  buttonHandler.loop();
  ambientSensor.loop();
  mp3Player.spin();
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