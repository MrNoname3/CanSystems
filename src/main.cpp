#include "main.hpp"

//--- Driver objects ---//
CanHandler canHandler(Serial, CAN_CS, CAN_INT, LED_PIN, FLASH_CS);
PushButtonHandler buttonHandler(Serial, canHandler, BUTTON_PIN);
RgbLedWrapper rgbLed(RGB_LED_NUM, RGB_PIN);
static constexpr uint32_t measureTimeMs = 1U * 60U * 1000U;
AmbientSensor ambientSensor(Serial, canHandler, LDR_PIN, measureTimeMs);
DFPlayer MP3Player(rgbLed, DFP_RX, DFP_TX, DFP_EN, DFP_BUSY);
const ExternalSensor extSensor(EXT_SENSOR_EN);

SerialIR swSerial(RS232_RX, RS232_TX);

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
  MP3Player.volume(15U);                                                      // Set MP3 player volume.
  MP3Player.play(1U);
  ambientSensor.begin();
  extSensor.on();
  Serial.println(F("********\r\nLooping..."));
  canHandler.ledOff();
}

void loop() {
  while(swSerial.available() > 0) {
    Serial.println(swSerial.read());
  }

  canHandler.loop();
  buttonHandler.loop();
  ambientSensor.loop();
  MP3Player.spin();
}

/*
  case CanCmd::FILET_START: {
    const uint16_t fwSize =  (uint16_t)(canFrameIn.data[0] | (canFrameIn.data[1] << 8));
    Serial.print(F("OTA:"));
    if(ota.start(fwSize)) {
      Serial.println(OK_STATE);
      sendCanResponse(&canFrameOut);
    }
    else {
      Serial.println(ERR_STATE);
    }
  } break;

  case CanCmd::FILET_SEND: {
    Serial.println(F("Store:"));
    if(ota.storeNextData(reinterpret_cast<OTA<otaFlashBegin, otaFwPiece>::FwPiece*>(canFrameIn.data))) {
      Serial.println(OK_STATE);
    }
    else {
      Serial.println(ERR_STATE);
    }
    canFrameOut.data[0] = lowByte(ota.getAddressW());
    canFrameOut.data[1] = highByte(ota.getAddressW());
    sendCanResponse(&canFrameOut);
  } break;

  case CanCmd::FILET_END: {
    ota.end();
  } break;
*/

void canMessageArrived(uint16_t command, const uint8_t (&data)[8]) {
  switch(command) {
    case static_cast<uint16_t>(CanCmd::RGB_LED): {
      rgbLed.setColor(data[0], data[1], data[2]);
      canHandler.send(static_cast<uint16_t>(CanCmd::RGB_LED));
    } break;
    case static_cast<uint16_t>(CanCmd::PLAY_MP3): {
      const uint16_t songNum{static_cast<uint16_t>(data[0] | (data[1] << 8))};
      MP3Player.play(songNum);                                  // Add selected song to queue.
      MP3Player.volume(data[2]);                                // Set volume.
      canHandler.send(static_cast<uint16_t>(CanCmd::PLAY_MP3));
    } break;
  };
}

void btnEventHandling(const uint8_t& event) {}