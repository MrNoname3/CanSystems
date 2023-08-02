#include "main.hpp"

//--- Variables ---//
volatile uint8_t canProcess = 0;                                              // On CAN interrupt, it counts incoming packets.
uint32_t pingTimer = 0;                                                       // It stores the last ping time.
static constexpr uint16_t pingTime = 1500;                                    // Ping timeot time in ms.
Settings settings;                                                            // Struct of settings.
uint16_t newCanAddress = 0;                                                   // Store arrived new local CAN address.
CircularBuffer<CanFrame, 5> receivedCanFrames;
EEPROMHandler<Settings, 0> eepromHandler(&settings);

//--- Maintenance variables ---//
uint8_t cycleCostMax = 2;                                                     // Calculate and store max loop cost.
constexpr uint16_t canAddressSaveTime = 30 * 1000;                            // Save new CAN address timeout time.
uint32_t canAddressSaveTimer = 0;                                             // Save new CAN address timeout timer.
bool saveNewAddress = false;                                                  // Enable saving new CAN address.
uint16_t errorCode = 0;                                                       // Store occured error codes.

//--- WS2812 RGB LED ---//
NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> ledStrip(RGB_LED_NUM, RGB_PIN);  // Setup LED strip.
CircularBuffer<RGBValues, 3> RGBColorBuffer;                                  // Queue for RGB values.

//--- Button ---//
PushButton Button(200, 500, 70);                                              // Object to handle pushbutton events.

//--- MP3 player ---//
DFPlayer MP3Player(DFP_RX, DFP_TX, DFP_EN, DFP_BUSY);                         // Object to handle MP3 player device.

//--- Temperature and humidity ---//
bool enableCharging = true;                                                   // Enable/disable charge for external sensor.
SI7021 si7021;                                                                // I2C humidity and temperature sensor driver.
int16_t temperature = 0;                                                      // Temperature value.
uint8_t humidity = 0;                                                         // Humidity value.
uint8_t light = 0;                                                            // Light LDR value.

//--- Setup section ---//
void setup() {
  Serial.begin(115200);                                                       // Open serial port with the given baudrate.

  pinMode(LED, OUTPUT);                                                       // LED pin -> output.
  pinMode(CAN_INT, INPUT_PULLUP);                                             // CAN_INT pin -> input with pullup.
  pinMode(BUTTON, INPUT_PULLUP);                                              // Button pin -> input with pullup.
  pinMode(CHARGE_PIN, OUTPUT);                                                // Charge enable pin -> output.
  delay(1);                                                                   // Little delay.

  LED_H;                                                                      // Turn on LED.
  Serial.println(F("*************************"));
  Serial.println(F("Starting..."));                                           // Serial debug print.
  Serial.print(F("CPP: "));
  Serial.println(__cplusplus);
  Serial.print(F("FW: "));
  Serial.println(SW_VERSION);

  ledStrip.Begin();                                                           // Clear LEDs
  ledStrip.Show();                                                            // and show it.

  Serial.print(F("EEPROM data"));
  if(eepromHandler.load()) {
    Serial.println(OK_STATE);                                                 // If init ok, print OK.
  }
  else {
    Serial.println(ERR_STATE);                                                // If can't init CAN controller, print ERROR.
  }

  Serial.print(F("CAN"));                                                     // Serial debug print.
  CAN.setClockFrequency(8e6);                                                 // SPI CAN controller runs from 8MHz crystal.
  if(!static_cast<bool>(CAN.begin(500E3))) {                                  // Set CAN speed to 500Kb/s.
    Serial.println(ERR_STATE);                                                // If can't init CAN controller, print ERROR.
    bitSet(errorCode, static_cast<uint8_t>(errorTypes::ERR_CAN_INIT));        // Set error flag.
  }
  else {
    Serial.println(OK_STATE);                                                 // If init ok, print OK.
  }
  Serial.print(F("Address: "));                                               // Print CAN address.
  Serial.println(settings.canAddress);

  CanFrame canFrameOut;                                                       // CAN frame to send.
  canFrameOut.canId.from = settings.canAddress;
  canFrameOut.canId.cmd = static_cast<uint16_t>(canCmd::NODE_CB_RESTARTED);
  canFrameOut.canId.to_ = settings.canAddress;

  CAN.filterExtended(canFrameOut.canId.id, CAN_MASK);                         // Setup extended CAN ID filtering.

  canFrameOut.canId.to_ = broadcastAddress;
  receivedCanFrames.put(canFrameOut);

  analogReference(DEFAULT);                                                   // Setup analog reference to 5V.
  attachInterrupt(digitalPinToInterrupt(CAN_INT), canIrqHandler, FALLING);    // Setup interrupt pin for CAN controller.

  MP3Player.attachRGBController(addToRGBQueue);                               // Add RGB LED controller to MP3 driver.
  MP3Player.volume(15);                                                       // Set MP3 player volume.

  MP3Player.play(1);

  Serial.print(F("HumTemp"));                                                 // Serial debug print.
  if(si7021.begin()) {                                                        // Initialize the I2C sensors and ping them.
    Serial.println(OK_STATE);                                                 // If init ok, print OK.
  }
  else {
    Serial.println(ERR_STATE);                                                // If can't init, print ERROR.
    bitSet(errorCode, static_cast<uint8_t>(errorTypes::ERR_I2C_SENSOR_INIT)); // Set error flag.
  }
  Wire.setClock(400000);                                                      // Set I2C bus speed.
  Wire.setWireTimeout(10000, true);                                           // Set I2C timeout to 10ms.
  si7021.setPrecision(0x81);                                                  // Set humtemp sensor reading resolution.

  Serial.println(F("*************************"));                             // Debug prints.
  Serial.println(F("Loop starting..."));
  wdt_enable(WDTO_120MS);                                                     // Enable WDT timer.
  LED_L;                                                                      // Turn off LED.
}

void loop() {

  //--- Maintenance ---//
  uint32_t cycleTimer = millis();                                             // Save millis value for loop cost calculation.

  //--- Button press handling ---//
  uint8_t buttonState = Button.buttonCheck(millis(), digitalRead(BUTTON));    // Check button actual state.
  if(buttonState > 0) {                                                       // Filter unvalid states.
    CanFrame canFrameOut;                                                     // CAN frame to send.
    canFrameOut.canId.from = settings.canAddress;                             // Set frame ID for outgoing message.
    canFrameOut.canId.cmd = static_cast<uint16_t>(canCmd::BCMD_GET_BUTTON_EVENT);
    canFrameOut.canId.to_ = broadcastAddress;

    Serial.print(F("Button event: "));                                        // Debug prints.
    Serial.println(buttonState);

    sendCanResponse(&canFrameOut);                                            // Send answer.
  }

  //--- Handle received CAN messages ---//
  if(canProcess > 0) {                                                        // If the CAN controller interrupted.
    canProcess--;                                                             // Lower value.
    uint8_t recvSize = CAN.parsePacket();                                     // Process CAN message.
    CanFrame canFrame;                                                        // CAN frame for received message.
    canFrame.canId.id = CAN.packetId();                                       // ID of received CAN packet.

    if((CAN.available() > 0) && (recvSize > 0)) {                             // If the message has data,
      CAN.readBytes(canFrame.data, sizeof(canFrame.data));                    // read and store it.
    }

    if(CAN.packetExtended() == true) {                                        // Handle standard messages as broadcast messages.                           
      if(settings.canAddress != canFrame.canId.to_) {                         // Software CAN ID filter. (HW doesn't work properly.)
        Serial.print(F("SW filter rejected ID: "));                           // Debug print of dropped package.
        Serial.println(canFrame.canId.id);
        return;
      }
    }
    else {
      Serial.println(F("Standard CAN message dropped"));
    }

    receivedCanFrames.put(canFrame);                                          // Put received CAN frame to processing queue.
    pingTimer = millis();                                                     // Ping timer reload.
    LED_L;                                                                    // LED off.
  }

  //--- Processing the frames ---//
  if(receivedCanFrames.isEmpty() == false) {                                  // Check queue.
    CanFrame canFrameIn = receivedCanFrames.pop();                            // Get frame from queue.
    CanFrame canFrameOut;                                                     // CAN frame to send.
    canFrameOut.canId.from = settings.canAddress;                             // Set frame ID for outgoing message.
    canFrameOut.canId.cmd = canFrameOut.canId.cmd;
    canFrameOut.canId.to_ = canFrameIn.canId.from;

#ifdef DEBUG_ON
    Serial.println(F("Frame:"));
    Serial.println(canFrameIn.canId.id);
    Serial.println(canFrameIn.canId.from);
    Serial.println(canFrameIn.canId.cmd);
    Serial.println(canFrameIn.canId.to_);
    for(uint8_t i = 0; i < sizeof(canFrameIn.data); i++) {
      Serial.print(canFrameIn.data[i]);
      Serial.print(F(" "));
    }
    Serial.println();
#endif
    
    switch(static_cast<canCmd>(canFrameIn.canId.cmd)) {                       // Send the command in the switch.

      case canCmd::BCMD_IDLE: { } break;
      
      case canCmd::BCMD_PING: {                                               // Ping command.
        canFrameOut.data[0] = cycleCostMax;                                   // Send maximum cycle cost.
        canFrameOut.data[1] = lowByte(errorCode);                             // Send error code.
        canFrameOut.data[2] = highByte(errorCode);
        errorCode = 0;                                                        // Clear error code.
        sendCanResponse(&canFrameOut);                                        // Send answer.
      } break;

        case canCmd::BCMD_RESET: {                                            // Reset MCU.
        sendCanResponse(&canFrameOut);                                        // Send answer.
        resetCMD();                                                           // Call reset function.
      } break;

      case canCmd::BCMD_GET_FW_VERSION: {                                     // Send firmware version to master.
        memcpy(canFrameOut.data, SW_VERSION, strlen(SW_VERSION) + 1);
        sendCanResponse(&canFrameOut);                                        // Send answer.
      } break;

      case canCmd::BCMD_SETADDRESS: {                                         // Set new CAN address. Response: used address.
        newCanAddress = (uint16_t)(canFrameIn.data[0] | (canFrameIn.data[1] << 8)); // 0.->address lowbyte, 1.->address highbyte.
        newCanAddress &= 0x3FF;                                               // Can't be more than 1023.
        
        if(newCanAddress != settings.canAddress) {                            // Check if new address is equal to old or not.
          constexpr uint8_t ledColor = 200;
          
          saveNewAddress = true;                                              // Enable saving.
          MP3Player.detachRGBController();                                    // Prevent RGB state overwrite.
          addToRGBQueue(0, ledColor, 0);                                      // Turn on RGB LED as green.
          Serial.println(F("Wait for button press to save new CAN address!"));  // Debug print.
          canAddressSaveTimer = millis();                                     // Timer reload.
        }
        else {
          Serial.println(F("Address already used!"));                         // Print if address is already used.
        }
      } break;

      case canCmd::BCMD_RGB_LED: {                                            // Add RGB color values to queue.
        addToRGBQueue(canFrameIn.data[0], canFrameIn.data[1], canFrameIn.data[2]);  // Add color values to queue.
        sendCanResponse(&canFrameOut);                                        // Send answer.
      } break;

      case canCmd::BCMD_GET_BUTTON_EVENT: { } break;

      case canCmd::BCMD_LAST_ELEMENT: { } break;

      case canCmd::NODE_CB_RESTARTED: { } break;

      case canCmd::NODE_CB_LAST_ELEMENT: { } break;

      case canCmd::ECMD_PLAY_MP3: {                                           // Play MP3 song.
        MP3Player.play((uint16_t)(canFrameIn.data[0] | (canFrameIn.data[1] << 8))); // Add selected song to queue.
        MP3Player.volume(canFrameIn.data[2]);                                 // Set volume.
        sendCanResponse(&canFrameOut);                                        // Send answer.
      } break;

      case canCmd::ECMD_CHARGE_DISPLAY: {                                     // Enable/disable external sensor charge.
        enableCharging = static_cast<bool>(canFrameOut.data[0]);              // Save requested charging state.
        sendCanResponse(&canFrameOut);                                        // Send answer.
      } break;

      case canCmd::ECMD_READ_HUMTEMP: {                                       // Send humidity and temperature value.
        canFrameOut.data[0] = lowByte(temperature);                           // Set values for response.
        canFrameOut.data[1] = highByte(temperature);
        canFrameOut.data[2] = humidity;
        sendCanResponse(&canFrameOut);                                        // Send answer.
      } break;

      case canCmd::ECMD_LAST_ELEMENT: { } break;

      default: {                                                              // Default case.
        Serial.println(F("Command is unhandled"));                            // If somehow program reach it, print it.
        bitSet(errorCode, static_cast<uint8_t>(errorTypes::ERR_UNHANDLED_COMMAND)); // Set error flag.
      } break;

    }  // End of switch.
  }  // End of if statement.

  //--- Handling RGB LEDs ---//
  if(RGBColorBuffer.isEmpty() == false) {                                   // Check RGB color buffer.
    RGBValues RGBColor = RGBColorBuffer.pop();                              // Get a color set from the queue.
    ledStrip.ClearTo(RgbColor(RGBColor.red, RGBColor.green, RGBColor.blue));  // Set same color to all RGB LED.
    ledStrip.Show();                                                        // Send color data to RGB LED strip.
  }

  //--- Handling timers ---//
  if(millis() - pingTimer >= pingTime) {                                    // Check if ping timer is expired.
    LED_H;                                                                  // If yes, turn on the LED.
  }

  //--- Handle temperature and humidity sensors ---//
  if(buttonState == 4) { enableCharging = true; }                           // Check button state to handle external sensor.
  if(buttonState == 5) { enableCharging = false; }
  handleHumTempSensor();                                                    // Call I2C sensor handler.
  handleExtSensors();                                                       // Call external sensor handler.

  //--- Handling MP3 player ---//
  MP3Player.spin();                                                         // Take care of playing queue.

  //--- Maintenance ---//
  if(saveNewAddress) {                                                      // Check if saving is enabled.
    if(millis() - canAddressSaveTimer >= canAddressSaveTime) {              // Check saving timeout timer.
      saveNewAddress = false;                                               // Disable saving.
      Serial.println(F("Address saving timeout!"));                         // Debug print.
      addToRGBQueue(0, 0, 0);                                               // Turn off RGB LED.
      MP3Player.attachRGBController(addToRGBQueue);                         // Enable RGB state overwrite.
    }
    if(buttonState == 3) {
      settings.canAddress = newCanAddress;                                  // Setup new can address to struct.

      Serial.print(F("Saving new CAN address"));
      if(eepromHandler.save()) {
        CanId canId;                                                        // Struct of CAN id.
        canId.from = 0;                                                     // Assemble CAN ID.
        canId.cmd = 0;
        canId.to_ = settings.canAddress;
        CAN.filterExtended(canId.id, CAN_MASK);                             // Setup filter for new CAN address.
        Serial.println(OK_STATE);
      }
      else {
        Serial.println(ERR_STATE);
      }
      
      saveNewAddress = false;                                               // Disable saving.
      addToRGBQueue(0, 0, 0);                                               // Turn off RGB LED.
      MP3Player.attachRGBController(addToRGBQueue);                         // Enable RGB state overwrite.
    }
  }

  uint8_t cycleCost = millis() - cycleTimer;                                // Calculate cysle cost.
  if(cycleCost > cycleCostMax) {                                            // If it is above max.
    cycleCostMax = cycleCost;                                               // Save the new max.
    Serial.print(F("Max cost: "));                                          // Debug print.
    Serial.println(cycleCostMax);
  }

  wdt_reset();                                                              // Reset the watchdog timer.
}

void handleHumTempSensor() {
  static si7021States sensorState = si7021States::READ_TEMPERATURE;         // Sensor reading state.

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
      humidity = static_cast<uint8_t>(si7021.getHumidityPercent());         // Read humidity.
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

void handleExtSensors() {
  constexpr uint8_t sensorOnLightValue = 10;                            // Light value to turn on the ext sensor.
  constexpr uint8_t sensorOffLightValue = 5;                            // Light value to turn off the ext sensor.
  constexpr uint16_t sensorStartDelay = 8000;                           // Startup delay value in ms for ext sensor.
  constexpr uint8_t adcInputFilterAlpha = 10;                           // Complementer filter ALPHA value.
  static uint32_t sensorStartupTimer = 0;                               // Timer to delay the sensor activation.
  
  uint8_t lightRaw = analogRead(A6) >> 2;                               // Analog read and map from 10bit to 8bit.
  light = ((adcInputFilterAlpha * lightRaw) + (100 - adcInputFilterAlpha) * light) / 100; // Complementer filter calculation.

  if(millis() - sensorStartupTimer >= sensorStartDelay) {               // Without it the ext sensor sometimes become unstable due restarts.
    if(light > sensorOnLightValue && enableCharging) {                  // On daylight...
      digitalWrite(CHARGE_PIN, HIGH);                                   // turn on the external sensor.
    }
    else {
      if(light < sensorOffLightValue || !enableCharging) {              // On night time...
        digitalWrite(CHARGE_PIN, LOW);                                  // Turn off the ext sensor.
        sensorStartupTimer = millis();                                  // Reload timer.
      }
    }     
  }
}

void addToRGBQueue(const uint8_t red, const uint8_t green, const uint8_t blue) {
  RGBValues RGBColor;
  RGBColor.red = red;                                     // Set color values.
  RGBColor.green = green;
  RGBColor.blue = blue;
  RGBColorBuffer.put(RGBColor);                           // Add to queue.
}

void sendCanResponse(CanFrame* canFrameOut) {
  bitWrite(errorCode, static_cast<uint8_t>(errorTypes::ERR_CAN_ID_SET),
    !CAN.beginExtendedPacket(canFrameOut->canId.id));                             // Set extended ID.
  bitWrite(errorCode, static_cast<uint8_t>(errorTypes::ERR_CAN_DATA_WRITE),
    !CAN.write(canFrameOut->data, sizeof(canFrameOut->data)));                    // Set data.
  bitWrite(errorCode, static_cast<uint8_t>(errorTypes::ERR_CAN_ENDPACKET),
    !CAN.endPacket());                                                            // Send packet.
}

void resetCMD() {
  Serial.println(F("Restarting..."));
  Serial.flush();                                         // Sends out data from serial buffer, before reset.
  resetFunc();                                            // Call reset function.
}

void canIrqHandler() {                                    // CAN controller iterrupt rutin handler.
  canProcess++;                                           // Count incoming packets.
}
