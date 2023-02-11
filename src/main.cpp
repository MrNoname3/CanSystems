#include "main.hpp"


//--- Variables ---//
volatile uint8_t canProcess = 0;                                            // On CAN interrupt, it counts incoming packets.
uint32_t pingTimer = 0;                                                     // It stores the last ping time.
const uint16_t pingTime = 1500;                                             // Ping timeot time in ms.
struct Settings settings;                                                   // Struct of settings.
uint16_t newCanAddress = 0;                                                 // Store arrived new local CAN address.
CircularBuffer<uint16_t, 10> canCommandBuffer;                              // State machine execution queue.
CircularBuffer<canCb, 10> canCallbackBuffer;                                // Callback message buffer for master.

//--- Maintenance variables ---//
uint8_t cycleCostMax = 2;                                                   // Calculate and store max loop cost.
constexpr uint16_t canAddressSaveTime = 30 * 1000;                          // Save new CAN address timeout time.
uint32_t canAddressSaveTimer = 0;                                           // Save new CAN address timeout timer.
bool saveNewAddress = false;                                                // Enable saving new CAN address.
uint16_t errorCode = 0;                                                     // Store occured error codes.

//--- WS2812 RGB LED ---//
CRGB rgbLeds[RGB_LED_NUM];                                                  // Define LED struct.
CircularBuffer<RGBValues, 5> RGBColorBuffer;                                // Queue for RGB values.

//--- Button ---//
PushButton Button(200, 500, 70);                                            // Object to handle pushbutton events.
CircularBuffer<uint8_t, 10> buttonEventBuffer;                              // Store button events.

//--- MP3 player ---//
DFPlayer MP3Player(DFP_RX, DFP_TX, DFP_EN, DFP_BUSY);                       // Object to handle MP3 player device.

//--- Temperature and humidity ---//
bool enableCharging = true;                                                 // Enable/disable charge for external sensor.
SI7021 si7021;                                                              // I2C humidity and temperature sensor driver.
int16_t temperature = 0;                                                    // Temperature value.
uint16_t humidity = 0;                                                      // Humidity value.

//--- Setup section ---//
void setup() {
  wdt_disable();                                                            // Disable WDT (Watchdog timer).
  Serial.begin(115200);                                                     // Open serial port with the given baudrate.

  pinMode(LED, OUTPUT);                                                     // LED pin -> output.
  pinMode(CAN_INT, INPUT_PULLUP);                                           // CAN_INT pin -> input with pullup.
  pinMode(BUTTON, INPUT_PULLUP);                                            // Button pin -> input with pullup.
  pinMode(CHARGE_PIN, OUTPUT);                                              // Charge enable pin -> output.
  delay(1);
  digitalWrite(CHARGE_PIN, HIGH);                                           // Turn ON charging.

  LED_H;                                                                    // Turn on LED.
  Serial.println(F("*************************"));
  Serial.println(F("Starting..."));                                         // Serial debug print.
  Serial.print(F("FW: "));
  Serial.println(F(SW_VERSION));

  FastLED.addLeds<CHIP_SET, RGB_PIN, COLOR_CODE>(rgbLeds, RGB_LED_NUM);     // Setup LED strip.
  FastLED.clear();                                                          // Clear LEDs
  FastLED.show();                                                           // and show it.

  LoadFromEEPROM();                                                         // Loads stored data from EEPROM.

  Serial.print(F("CAN"));                                                   // Serial debug print.
  CAN.setClockFrequency(8e6);                                               // SPI CAN controller runs from 8MHz crystal.
  if(!static_cast<bool>(CAN.begin(500E3))) {                                // Set CAN speed to 500Kb/s.
    Serial.println(ERR_STATE);                                              // If can't init CAN controller, print ERROR.
    bitSet(errorCode, static_cast<uint8_t>(errorTypes::ERR_CAN_INIT));      // Set error flag.
  }
  else {
    Serial.println(OK_STATE);                                               // If init ok, print OK.
  }
  CAN.filterExtended(dataToExtId(0, 0, settings.canAddress), CAN_MASK);     // Setup extended CAN ID filtering.

  analogReference(DEFAULT);                                                 // Setup analog reference to 5V.
  attachInterrupt(digitalPinToInterrupt(CAN_INT), canIrqHandler, FALLING);  // Setup interrupt pin for CAN controller.

  canCommandBuffer.put(static_cast<uint16_t>(canCmdB::BCMD_IDLE));          // Set state machine default command.
  canCallbackBuffer.put(canCb::NODE_CB_RESTARTED);                          // Set callback state as restarted.

  MP3Player.attachRGBController(addToRGBQueue);                             // Add RGB LED controller to MP3 driver.
  MP3Player.volume(15);                                                     // Set MP3 player volume.

  Serial.print(F("HumTemp"));                                               // Serial debug print.
  if(si7021.begin()) {                                                      // Initialize the I2C sensors and ping them.
    Serial.println(OK_STATE);                                               // If init ok, print OK.
  }
  else {
    Serial.println(ERR_STATE);                                              // If can't init, print ERROR.
    bitSet(errorCode, static_cast<uint8_t>(errorTypes::ERR_I2C_SENSOR_INIT));  // Set error flag.
  }
  Wire.setClock(400000);                                                    // Set I2C bus speed.
  Wire.setWireTimeout(10000, true);                                         // Set I2C timeout to 10ms.
  si7021.setPrecision(0x81),                                                // Set humtemp sensor reading resolution.

  Serial.println(F("*************************"));                           // Debug prints.
  Serial.println(F("Loop starting..."));
  wdt_enable(WDTO_30MS);                                                    // Enable WDT timer.
  LED_L;                                                                    // Turn off LED.
}

void loop() {

  //--- Maintenance ---//
  uint32_t cycleTimer = millis();                                           // Save millis value for loop cost calculation.

  //--- Button press handling ---//
  uint8_t buttonState = Button.buttonCheck(millis(), digitalRead(BUTTON));  // Check button actual state.
  if(buttonState > 0) {                                                     // Filter unvalid states.
    buttonEventBuffer.put(buttonState);                                     // Store the button event.
    canCommandBuffer.put(static_cast<uint16_t>(canCmdB::BCMD_GET_BUTTON_EVENT)); // Put command to queue.
    Serial.print(F("Button event: "));                                      // Debug prints.
    Serial.println(buttonState);
  }

  //--- Processing CAN messages ---//
  uint8_t recvData[8] = { 0 };                                              // We will store the message in this variable.
  uint8_t canMsg[8] = { 0 };                                                // Response message data.
  uint16_t masterAddress = DEFAULT_MASTER_ADDRESS;                          // Master CAN address.
  uint16_t cmdExec = static_cast<uint16_t>(canCmdB::BCMD_IDLE);             // Execution command for the state machine.
  uint32_t extendedIdOut = 0;                                               // Extended CAN ID to send.

  if(canProcess > 0) {                                                      // If the CAN controller interrupted.
    canProcess--;                                                           // Lower value.
    uint8_t recvSize = CAN.parsePacket();                                   // Process CAN message length.
    uint32_t canID = CAN.packetId();                                        // Received CAN packet ID.
    uint16_t localAddress = 0;                                              // Local CAN address.
    uint16_t cmd = 0;                                                       // CAN command.

    if((CAN.available() > 0) && (recvSize > 0)) {
      CAN.readBytes(recvData, sizeof(recvData));                            // Read the message.
    }

    if(CAN.packetExtended() == true) {                                      // Handle standard messages as broadcast messages.
      extIdToData(canID, &masterAddress, &cmd, &localAddress);              // Extract data from ID.

      if(settings.canAddress != localAddress) {                             // Software CAN ID filter. (HW doesn't work properly.)
        Serial.print(F("SW filter rejected ID: "));                         // Debug print of dropped package.
        Serial.println(canID);
        return;
      }
    }
    else {
      Serial.println(F("Standard CAN message dropped"));
    }

    canCommandBuffer.put(cmd);                                              // Put received command to queue.
    pingTimer = millis();                                                   // Ping timer reload.
    LED_L;                                                                  // LED off.
  }  // End of if state (processing CAN).

  if(canCommandBuffer.isEmpty() == false) {                                 // If available, get next state from
    cmdExec = canCommandBuffer.pop();                                       // execution queue.
    extendedIdOut = dataToExtId(settings.canAddress,
      static_cast<uint8_t>(cmdExec), masterAddress);                        // Make extended CAN ID to send.
  }

  switch(cmdExec) {                                                         // Send the command in the switch.

    case static_cast<uint16_t>(canCmdB::BCMD_IDLE): {                       // Idle state.

    } break;

    case static_cast<uint16_t>(canCmdB::BCMD_PING): {                       // Ping command.
      if(canCallbackBuffer.isEmpty() == false) {                            // Check if callback needed.
        uint16_t cbNum = static_cast<uint16_t>(canCallbackBuffer.pop());    // Get callback number.
        canMsg[0] = lowByte(cbNum);                                         // Send the callback type to the master.
        canMsg[1] = highByte(cbNum);
        canMsg[2] = cycleCostMax;                                           // Send maximum cycle cost.
        canMsg[3] = lowByte(errorCode);                                     // Send error code.
        canMsg[4] = highByte(errorCode);
        errorCode = 0;                                                      // Clear error code.
      }
      sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));               // Send answer.
    } break;

    case static_cast<uint16_t>(canCmdB::BCMD_RESET): {                      // Reset MCU.
      sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));               // Send answer.
      resetCMD();                                                           // Call reset function.
    } break;

    case static_cast<uint16_t>(canCmdB::BCMD_GET_FW_VERSION): {             // Send firmware version to master.
      memcpy(canMsg, SW_VERSION, sizeof(SW_VERSION));
      sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));               // Send answer.
    } break;

    case static_cast<uint16_t>(canCmdB::BCMD_SETADDRESS): {                 // Set new CAN address. Response: used address.
      newCanAddress = (uint16_t)(recvData[0] | (recvData[1] << 8));         // 0.->address lowbyte, 1.->address highbyte.
      newCanAddress &= 0x3FF;                                               // Can't be more than 1023.
      if(newCanAddress != settings.canAddress) {                            // Check if new address is equal to old or not.
        saveNewAddress = true;                                              // Enable saving.
        MP3Player.detachRGBController();                                    // Prevent RGB state overwrite.
        addToRGBQueue(0, 200, 0);                                           // Turn on RGB LED as green.
        Serial.println(F("Wait for button press to save new CAN address!"));  // Debug print.
        canAddressSaveTimer = millis();                                     // Timer reload.
      }
      else {
        Serial.println(F("Address already used!"));                         // Print if address is already used.
      }
    } break;

    case static_cast<uint16_t>(canCmdB::BCMD_RGB_LED): {                    // Add RGB color values to queue.
      addToRGBQueue(recvData[0], recvData[1], recvData[2]);                 // Add color values to queue.
      sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));               // Send answer.
    } break;

    case static_cast<uint16_t>(canCmdB::BCMD_GET_BUTTON_EVENT): {           // Send button event.
      if(buttonEventBuffer.isEmpty() == false) {                            // Check if callback needed.
        canMsg[0] = buttonEventBuffer.pop();                                // Send button event.
      }
      sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));               // Send answer.
    } break;

    case static_cast<uint16_t>(canCmdE::ECMD_PLAY_MP3): {                   // Play MP3 song.
      MP3Player.play((uint16_t)(recvData[0] | (recvData[1] << 8)));         // Add selected song to queue.
      MP3Player.volume(recvData[2]);                                        // Set volume.
      sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));               // Send answer.
    } break;

    case static_cast<uint16_t>(canCmdE::ECMD_CHARGE_DISPLAY): {             // Enable/disable external sensor charge.
      enableCharging = static_cast<bool>(recvData[0]);                      // Save requested charging state.
      sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));               // Send answer.
    } break;

    case static_cast<uint16_t>(canCmdE::ECMD_READ_HUMTEMP): {               // Send humidity and temperature value.
      canMsg[0] = lowByte(temperature);                                     // Set values for response.
      canMsg[1] = highByte(temperature);
      canMsg[2] = static_cast<uint8_t>(humidity);
      sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));               // Send answer.
    } break;

    default: {                                                              // Default case.
      Serial.println(F("Command is unhandled"));                            // If somehow program reach it, print it.
      bitSet(errorCode, static_cast<uint8_t>(errorTypes::ERR_UNHANDLED_COMMAND)); // Set error flag.
    } break;

  }  // End of switch.

  //--- Handling RGB LEDs ---//
  if(RGBColorBuffer.isEmpty() == false) {                                   // Check RGB color buffer.
    RGBValues RGBColor = RGBColorBuffer.pop();                              // Get a color set from the queue.
    for(uint8_t i = 0; i < RGB_LED_NUM; i++) {                              // Set same color to all RGB LED.
      rgbLeds[i].setRGB(RGBColor.red, RGBColor.green, RGBColor.blue);
    }
    FastLED.show();                                                         // Send color data to RGB LED strip.
  }

  //--- Handling timers ---//
  if(millis() - pingTimer >= pingTime) {                                    // Check if ping timer is expired.
    LED_H;                                                                  // If yes, turn on the LED.
  }

  //--- Handle temperature and humidity sensors ---//
  handleHumTempSensor();                                                    // Call I2C sensor handler.
  handleCharging();                                                         // Call external sensor handler.

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
      settings.isValid = EEPROM_VALID;                                      // Setup validity flag to struct.
      settings.canAddress = newCanAddress;                                  // Setup new can address to struct.
      if(!SaveToEEPROM()) {                                                 // Save struct to EEPROM.
        LoadFromEEPROM();                                                   // If not success, load old settings.
      }
      CAN.filterExtended(dataToExtId(0, 0, settings.canAddress), CAN_MASK); // Setup filter for new CAN address.
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
      static uint32_t sensorReadingTimer = 0;                               // Sensor reading timer.
      if(millis() - sensorReadingTimer >= 60000) {                          // Check timer.
        sensorState = si7021States::READ_TEMPERATURE;                       // Set new state.
        sensorReadingTimer = millis();                                      // Reload timer.
      }
    } break;

    case si7021States::READ_TEMPERATURE: {
      temperature = si7021.getCelsiusHundredths();                          // Read temperature.
      Serial.println(float(temperature) / 100); //TODO: delete this line after debug.
      sensorState = si7021States::READ_HUMIDITY;                            // Set new state.
    } break;

    case si7021States::READ_HUMIDITY: {
      humidity = si7021.getHumidityPercent();                               // Read humidity.
      Serial.println(humidity); //TODO: delete this line after debug.
      sensorState = si7021States::IDLE;                                     // Set new state.
    } break;

    default: {
      sensorState = si7021States::IDLE;                                     // Set new state.
    } break;
  }

  if(Wire.getWireTimeoutFlag()) {
    temperature = 0;                                                        // Delete (possible) unvalid value.
    humidity = 0;                                                           // Delete (possible) unvalid value.
    bitSet(errorCode, static_cast<uint8_t>(errorTypes::ERR_I2C_READ_TIMEOUT));  // Set error flag.
    Wire.clearWireTimeoutFlag();                                            // Clear I2C timeout flag.
    Serial.println(F("I2C timeout occured!"));                              // Debug print.
  }
}

void handleCharging() {
  static Charging charging = Charging::START;                               // Capacitor charging state.
  static uint32_t chargeControlTimer = millis();                            // Capacitor charge control timer.

  if(!enableCharging) { return; }                                           // Return if charging is not enabled.

  switch(charging) {
    case Charging::START: {                                                 // Capacitor initial charging state.
      if(millis() - chargeControlTimer >= 300000) {                         // Check timer.
        digitalWrite(CHARGE_PIN, LOW);                                      // Disable charging.
        charging = Charging::DISCHARGE;                                     // Set next state.
        chargeControlTimer = millis();                                      // Reload timer.
      }
    } break;

    case Charging::CHARGE: {                                                // Capacitor charging state.
      if(millis() - chargeControlTimer >= 120000) {                         // Check timer.
        digitalWrite(CHARGE_PIN, LOW);                                      // Disable charging.
        charging = Charging::DISCHARGE;                                     // Set next state.
        chargeControlTimer = millis();                                      // Reload timer.
      }
    } break;

    case Charging::DISCHARGE: {                                             // Capacitor discharging state.
      if(millis() - chargeControlTimer >= 900000) {                         // Check timer.
        digitalWrite(CHARGE_PIN, HIGH);                                     // Enable charging.
        charging = Charging::CHARGE;                                        // Set next state.
        chargeControlTimer = millis();                                      // Reload timer.
      }
    } break;

    default: {                                                              // Default state.
      charging = Charging::START;                                           // Set next state.
      chargeControlTimer = millis();                                        // Reload timer.
    } break;
  }
}

void addToRGBQueue(uint8_t red, uint8_t green, uint8_t blue) {
  RGBValues RGBColor;
  RGBColor.red = red;                                                   // Set color values.
  RGBColor.green = green;
  RGBColor.blue = blue;
  RGBColorBuffer.put(RGBColor);                                         // Add to queue.
}

void sendCanResponse(uint32_t extId, const uint8_t data[], uint8_t size) {
  bitWrite(errorCode, static_cast<uint8_t>(errorTypes::ERR_CAN_ID_SET),
    !CAN.beginExtendedPacket(extId));                                   // Set extended ID.
  bitWrite(errorCode, static_cast<uint8_t>(errorTypes::ERR_CAN_DATA_WRITE),
    !CAN.write(data, size));                                            // Set data.
  bitWrite(errorCode, static_cast<uint8_t>(errorTypes::ERR_CAN_ENDPACKET),
    !CAN.endPacket());                                                  // Send packet.
}

void resetCMD() {
  Serial.println(F("Restarting..."));
  Serial.flush();                                                       // Sends out data from serial buffer, before reset.
  resetFunc();                                                          // Call reset function.
}

void LoadFromEEPROM() {
  EEPROM.get(0, settings);                                // Reading data from EEPROM to settings struct.
  if(settings.isValid == EEPROM_VALID) {                  // If can address is valid, use it.
    Serial.print(SAVED_STATE);                            // Print saved mark.
  }
  else {
    //settings.canAddress = DEFAULT_LOCAL_ADDRESS;          // If not, use default CAN address.
    Serial.print(DEFAULT_STATE);                          // Print default mark.
  }
  Serial.print(F("CAN address: "));                       // Print CAN address.
  Serial.println(settings.canAddress);
}

bool SaveToEEPROM() {
  bool ret = false;                                       // Return value.
  const uint16_t cooldownTime = 10000;                    // EEPROM write cooldown time.
  static uint32_t cooldownTimer = 0;                      // Cooldown timer.
  static bool firstRun = true;                            // Allow first change without cooldown.
  if((millis() - cooldownTimer > cooldownTime) || firstRun) {  // Timer expiration check.
    cooldownTimer = millis();                             // Reload timer.
    EEPROM.put(0, settings);                              // Save settings struct to EEPROM.
    Serial.println(F("Saved!"));                          // Debug print.
    firstRun = false;
    ret =  true;                                          // Set return value.
  }
  else {                                                  // If EEPROM write is on cooldown,
    Serial.println(F("Not saved! EEPROM cooldown!"));     // print it
  }
  return ret;                                             // Return with the result.
}

uint32_t dataToExtId(uint16_t from, uint16_t cmd, uint16_t to_) {
  from &= 0x3FF;                                          // 10bit
  cmd &= 0x1FF;                                           // 9bit
  to_ &= 0x3FF;                                           // 10bit
  return (uint32_t)(((uint32_t)from << 0) | ((uint32_t)cmd << 10) | ((uint32_t)to_ << 19));
}

void extIdToData(uint32_t extId, uint16_t* from, uint16_t* cmd, uint16_t* to_) {
  extId &= 0x1fffffff;                                    // 29bit
  *from = (uint16_t)((extId >> 0) & 0x3FF);               // 10bit
  *cmd = (uint16_t)((extId >> 10) & 0x1FF);               // 9bit
  *to_ = (uint16_t)((extId >> 19) & 0x3FF);               // 10bit
}

void canIrqHandler() {                                    // CAN controller iterrupt rutin handler.
  canProcess++;                                           // Count incoming packets.
}
