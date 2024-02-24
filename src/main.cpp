#include "main.hpp"

//--- CAN handler ---//
CanHandler canHandler(Serial, CAN_CS, CAN_INT, LED);

//--- Variables ---//

//--- Maintenance variables ---//
uint8_t cycleCostMax = 2;                                                     // Calculate and store max loop cost.
uint16_t errorCode = 0;                                                       // Store occured error codes.

//--- WS2812 RGB LED ---//
NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> ledStrip(RGB_LED_NUM, RGB_PIN);  // Setup LED strip.
CircularBuffer<RGBValues, 3> RGBColorBuffer;                                  // Queue for RGB values.

//--- Button ---//
PushButton Button(300, 500, 70);                                              // Object to handle pushbutton events.

//--- SPI and OTA ---//
static constexpr uint8_t otaFlashBegin = 0;                                   // Flash begin address for OTA.
static constexpr uint8_t otaFwPiece = 4;                                      // Size of FW chunks in bytes.
SPIFlash flash(FLASH_CS, 0xEF40);                                             // SPI FLASH driver. (0xEF40 -> Windbond 64mbit flash.)
OTA<otaFlashBegin, otaFwPiece> ota(&flash, calculateCRC16);                   // OTA handler.

//--- MP3 player ---//
DFPlayer MP3Player(DFP_RX, DFP_TX, DFP_EN, DFP_BUSY);                         // Object to handle MP3 player device.

//--- Temperature, humidity and light ---//
SI7021 si7021;                                                                // I2C humidity and temperature sensor driver.
int16_t temperature = 0;                                                      // Temperature value.
uint16_t humidity = 0;                                                        // Humidity value.
uint8_t light = 0;                                                            // Light value.

SerialIR swSerial(RS232_RX, RS232_TX);

//--- Setup section ---//
void setup() {
  Serial.begin(MONITOR_BAUD);                                                 // Open serial port with the given baudrate.
  pinMode(EXT_SENSOR_EN, OUTPUT);                                             // External sensor enable pin -> output.
  delay(1);
  canHandler.ledOn();
  digitalWrite(EXT_SENSOR_EN, HIGH);

  Serial.println(F("*************************"));
  Serial.println(F("Starting..."));                                           // Serial debug print.
  canHandler.begin(500E3);                                                    // Set CAN speed to 500Kb/s.

  ledStrip.Begin();                                                           // Clear LEDs
  ledStrip.Show();                                                            // and show it.

  Serial.print(F("FLASH"));
  flash.initialize() ? Serial.println(OK_STATE) : Serial.println(ERR_STATE);    // Initialise SPI FLASH.

  analogReference(DEFAULT);                                                   // Setup analog reference to 5V.
  bitSet(ADCSRA, ADPS2);                                                      // Fast ADC, set prescaler to 16.
  bitSet(ADCSRA, ADPS1);
  bitClear(ADCSRA, ADPS0);

  MP3Player.attachRGBController(addToRGBQueue);                               // Add RGB LED controller to MP3 driver.
  MP3Player.volume(15);                                                       // Set MP3 player volume.

  MP3Player.play(1);

  Wire.setClock(400000);                                                      // Set I2C bus speed.
  Wire.setWireTimeout(10000, true);                                           // Set I2C timeout to 10ms.
  Serial.print(F("HumTemp"));                                                 // Serial debug print.
  if(si7021.begin()) {                                                        // Initialize the I2C sensor and ping it.
    Serial.println(OK_STATE);                                                 // If init ok, print OK.
  }
  else {
    Serial.println(ERR_STATE);                                                // If can't init, print ERROR.
    bitSet(errorCode, static_cast<uint8_t>(errorTypes::ERR_I2C_SENSOR_INIT)); // Set error flag.
  }
  si7021.setPrecision(0x81);                                                  // Set humtemp sensor reading resolution.

  Serial.println(F("*************************"));                             // Debug prints.
  Serial.println(F("Loop starting..."));
  canHandler.ledOff();
}

void loop() {

  //--- Maintenance ---//
  uint32_t cycleTimer = millis();                                             // Save millis value for loop cost calculation.

  while(swSerial.available() > 0) {
    Serial.println(swSerial.read());
  }

  //--- Button press handling ---//
  uint8_t buttonState = Button.buttonCheck(millis(), analogRead(BUTTON) > 511 ? HIGH : LOW);  // Check button actual state.
  if(buttonState > 0) {                                                       // Filter unvalid states.
    //CanFrame canFrameOut;                                                     // CAN frame to send.
    //canFrameOut.canId.from = settings.canAddress;                             // Set frame ID for outgoing message.
    //canFrameOut.canId.cmd = static_cast<uint16_t>(CanCmd::BUTTON_EVENT);
    //canFrameOut.canId.to_ = broadcastAddress;

    Serial.print(F("Button event: "));                                        // Debug prints.
    Serial.println(buttonState);

    //sendCanResponse(&canFrameOut);                                            // Send answer.
  }

  canHandler.loop();

/*
  //--- Processing the frames ---//
  case CanCmd::RGB_LED: {
    addToRGBQueue(canFrameIn.data[0], canFrameIn.data[1], canFrameIn.data[2]);  // Add color values to queue.
    sendCanResponse(&canFrameOut);
  } break;

  case CanCmd::BUTTON_EVENT: { } break;

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

  case CanCmd::PLAY_MP3: {
    MP3Player.play((uint16_t)(canFrameIn.data[0] | (canFrameIn.data[1] << 8))); // Add selected song to queue.
    MP3Player.volume(canFrameIn.data[2]);                                 // Set volume.
    sendCanResponse(&canFrameOut);
  } break;

  case CanCmd::READ_HUM_TEMP_LDR: {
    canFrameOut.data[0] = lowByte(temperature);                           // Set values for response.
    canFrameOut.data[1] = highByte(temperature);
    canFrameOut.data[2] = lowByte(humidity);
    canFrameOut.data[3] = highByte(humidity);
    canFrameOut.data[4] = light;
    sendCanResponse(&canFrameOut);
  } break;
*/
  //--- Handling RGB LEDs ---//
  if(RGBColorBuffer.isEmpty() == false) {                                   // Check RGB color buffer.
    RGBValues RGBColor = RGBColorBuffer.pop();                              // Get a color set from the queue.
    ledStrip.ClearTo(RgbColor(RGBColor.red, RGBColor.green, RGBColor.blue));  // Set the same color for all RGB LED.
    ledStrip.Show();                                                        // Send color data to RGB LED strip.
  }

  //--- Handle temperature and humidity sensors ---//
  handleSensors();

  //--- Handling MP3 player ---//
  MP3Player.spin();

  //--- Maintenance ---//
  uint8_t cycleCost = millis() - cycleTimer;                                // Calculate cycle cost.
  if(cycleCost > cycleCostMax) {                                            // If it is above max,
    cycleCostMax = cycleCost;                                               // save the new max.
    Serial.print(F("Max cost: "));                                          // Debug print.
    Serial.println(cycleCostMax);
  }
}

void handleSensors() {
  static si7021States sensorState = si7021States::READ_TEMPERATURE;         // Sensor reading state.
  constexpr uint8_t adcInputFilterAlpha = 10;                               // Complementer filter ALPHA value.
  uint8_t lightRaw = analogRead(LDR_PIN) >> 2;                              // Analog read and map from 10bit to 8bit.
  light = ((adcInputFilterAlpha * lightRaw) + (100 - adcInputFilterAlpha) * light) / 100; // Complementer filter calculation.

  switch(sensorState) {
    case si7021States::IDLE : {
      constexpr uint16_t sensorReadingTime = 60000;
      static uint32_t sensorReadingTimer = 0;                               // Sensor reading timer.

      if(millis() - sensorReadingTimer >= sensorReadingTime) {              // Check timer.
        sensorState = si7021States::READ_TEMPERATURE;
        sensorReadingTimer = millis();                                      // Reload timer.
      }
    } break;

    case si7021States::READ_TEMPERATURE: {
      temperature = si7021.getCelsiusHundredths();                          // Read temperature.
      sensorState = si7021States::READ_HUMIDITY;
    } break;

    case si7021States::READ_HUMIDITY: {
      humidity = si7021.getHumidityPercent();                               // Read humidity.
      sensorState = si7021States::IDLE;
    } break;

    default: {
      sensorState = si7021States::IDLE;
    } break;
  }

  if(Wire.getWireTimeoutFlag()) {
    temperature = humidity = 0;                                             // Delete (possible) unvalid values.
    bitSet(errorCode, static_cast<uint8_t>(errorTypes::ERR_I2C_READ_TIMEOUT));  // Set error flag.
    Wire.clearWireTimeoutFlag();                                            // Clear I2C timeout flag.
    Serial.println(F("I2C timeout occured!"));
  }
}

void addToRGBQueue(const uint8_t red, const uint8_t green, const uint8_t blue) {
  RGBValues RGBColor;
  RGBColor.red = red;                                     // Set color values.
  RGBColor.green = green;
  RGBColor.blue = blue;
  RGBColorBuffer.put(RGBColor);                           // Add to queue.
}

uint16_t calculateCRC16(const uint8_t* data, uint16_t length) {
  constexpr uint16_t polynomial = 0x1021;
  uint16_t crc = 0;
  for(uint16_t i = 0; i < length; i++) {
    crc ^= ((uint16_t)data[i] << 8);
    for(uint8_t j = 0; j < 8; j++) {
      if(crc & 0x8000) {
        crc = (crc << 1) ^ polynomial;
      }
      else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

inline int analogReadFast(uint8_t ADCpin) {
  uint8_t ADCSRAoriginal = ADCSRA;
  ADCSRA = (ADCSRA & B11111000) | 4;
  int adc = analogRead(ADCpin);
  ADCSRA = ADCSRAoriginal;
  return adc;
}