#include "main.hpp"

//--- Driver objects ---//
CanHandler canHandler(Serial, CAN_CS, CAN_INT, LED_PIN, FLASH_CS);
PushButtonHandler buttonHandler(Serial, canHandler, BUTTON_PIN);
RgbLedWrapper rgbLed(RGB_LED_NUM, RGB_PIN);
static constexpr uint32_t measureTimeMs = 1U * 60U * 1000U;
AmbientSensor ambientSensor(Serial, canHandler, LDR_PIN, measureTimeMs);
DFPlayer MP3Player(rgbLed, DFP_RX, DFP_TX, DFP_EN, DFP_BUSY);
const ExternalSensor extSensor(EXT_SENSOR_EN);

//SerialIR swSerial(RS232_RX, RS232_TX);

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
  //MP3Player.volume(15U);                                                      // Set MP3 player volume.
  //MP3Player.play(1U);
  ambientSensor.begin();
  extSensor.on();
  Serial.println(F("********\r\nLooping..."));
  canHandler.ledOff();
}

void loop() {
  //while(swSerial.available() > 0) {
  //  Serial.println(swSerial.read());
  //}

  canHandler.loop();
  buttonHandler.loop();
  ambientSensor.loop();
  MP3Player.spin();
}

void canMessageArrived(uint16_t command, const uint8_t (&data)[8]) {
  switch(command) {
    case static_cast<uint16_t>(CanCmd::RGB_LED): {
      rgbLed.setColor(data[0], data[1], data[2], true);
      canHandler.send(static_cast<uint16_t>(CanCmd::RGB_LED));
    } break;
    case static_cast<uint16_t>(CanCmd::PLAY_MP3): {
      const uint16_t songNum{static_cast<uint16_t>(data[0] | (data[1] << 8))};
      MP3Player.play(songNum, data[2], data[3], data[4], data[5]);
      canHandler.send(static_cast<uint16_t>(CanCmd::PLAY_MP3));
    } break;
  };
}

void btnEventHandling(const uint8_t& event) {
  switch(event) {
    case 3: {
      rgbLed.setColor(50U, 50U, 50U, true);
    } break;
    case 4: {
      rgbLed.clear();
    } break;
    default: {} break;
  }
}