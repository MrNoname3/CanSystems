#include "main.hpp"

//--- CAN handler ---//
CanHandler canHandler(Serial, CAN_CS, CAN_INT, LED, FLASH_CS);

//--- WS2812 RGB LED ---//
NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> ledStrip(RGB_LED_NUM, RGB_PIN);  // Setup LED strip.

//--- Button ---//
PushButton Button(300, 500, 70);                                              // Object to handle pushbutton events.

//--- SPI and OTA ---//
//static constexpr uint8_t otaFlashBegin = 0;                                   // Flash begin address for OTA.
//static constexpr uint8_t otaFwPiece = 4;                                      // Size of FW chunks in bytes.
//SPIFlash flash(FLASH_CS, 0xEF40);                                             // SPI FLASH driver. (0xEF40 -> Windbond 64mbit flash.)
//OTA<otaFlashBegin, otaFwPiece> ota(&flash, calculateCRC16);                   // OTA handler.

//--- MP3 player ---//
DFPlayer MP3Player(DFP_RX, DFP_TX, DFP_EN, DFP_BUSY);                         // Object to handle MP3 player device.

//--- Temperature, humidity and light ---//
static constexpr uint32_t measureTimeMs = 1U * 60U * 1000U;
AmbientSensor ambientSensor(Serial, canHandler, LDR_PIN, measureTimeMs);

SerialIR swSerial(RS232_RX, RS232_TX);

//--- Setup section ---//
void setup() {
  Serial.begin(MONITOR_BAUD);                                                 // Open serial port with the given baudrate.
  canHandler.ledOn();
  canHandler.addCanCallback(canMessageArrived);
  pinMode(EXT_SENSOR_EN, OUTPUT);                                             // External sensor enable pin -> output.
  delay(1);
  Serial.println(F("\r\n********\r\nStarting..."));
  canHandler.begin(500E3);                                                    // Set CAN speed to 500Kb/s.
  ledStrip.Begin();                                                           // Clear LEDs
  ledStrip.Show();                                                            // and show it.
  MP3Player.attachRGBController(setRgbLed);                                   // Add RGB LED controller to MP3 driver.
  MP3Player.volume(15);                                                       // Set MP3 player volume.
  MP3Player.play(1);
  ambientSensor.begin();
  digitalWrite(EXT_SENSOR_EN, HIGH);
  Serial.println(F("********\r\nLooping..."));
  canHandler.ledOff();
}

void loop() {
  while(swSerial.available() > 0) {
    Serial.println(swSerial.read());
  }

  //--- Button press handling ---//
  uint8_t buttonState = Button.buttonCheck(millis(), analogRead(BUTTON) > 511 ? HIGH : LOW);
  if(buttonState > 0) {
    Serial.print(F("Button event: "));
    Serial.println(buttonState);
    const uint8_t canData[8] = { buttonState, 0, 0, 0, 0, 0, 0, 0 };
    canHandler.send(CanCmd::BUTTON_EVENT, canData);
  }

  //--- Processing CAN frames ---//
  canHandler.loop();

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

  //--- Handle temperature and humidity sensors ---//
  //handleSensors();
  ambientSensor.loop();

  //--- Handling MP3 player ---//
  MP3Player.spin();
}

void canMessageArrived(uint16_t command, const uint8_t (&data)[8]) {
  switch(command) {
    case static_cast<uint16_t>(CanCmd::RGB_LED): {
      setRgbLed(data[0], data[1], data[2]);
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

void setRgbLed(const uint8_t red, const uint8_t green, const uint8_t blue) {
  ledStrip.ClearTo(RgbColor(red, green, blue));
  ledStrip.Show();
}