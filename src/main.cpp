#include "main.hpp"

//--- Constants ---//
#define SW_VERSION "V1.0.0"                                                 // Actual software version.
#define EEPROM_VALID 231                                                    // EEPROM validity check.
#define DEFAULT_LOCAL_ADDRESS 444                                           // Node default address if no saved available.
#define DEFAULT_MASTER_ADDRESS 10                                           // Default CAN master address.
#define CAN_MASK 0x1FF80000                                                 // CAN extended ID mask.
#define OK_STATE " [ OK ]"                                                  // OK status.
#define ERR_STATE " [ ERR ]"                                                // Error status.
#define SAVED_STATE "[S] "                                                  // Saved data mark.
#define DEFAULT_STATE "[D] "                                                // Default data mark.

//--- Variables ---//
volatile uint8_t canProcess = 0;                                            // On CAN interrupt, it counts incoming packets.
uint32_t pingTimer = 0;                                                     // It stores the last ping time.
const uint16_t pingTime = 1500;                                             // Ping timeot time in ms.
struct Settings settings;                                                   // Struct of settings.
CircularBuffer<canCmd, 10> canCommandBuffer;                                // State machine execution queue.
CircularBuffer<canCb, 10> canCallbackBuffer;                                // Callback message buffer for master.

//--- Maintenance variables ---//
uint8_t cycleCostMax = 0;                                                   // Calculate and store max loop cost.
constexpr uint16_t canAddressSaveTime = 30 * 1000;                          // Save new CAN address timeout time.
uint32_t canAddressSaveTimer = 0;                                           // Save new CAN address timeout timer.
bool saveNewAddress = false;                                                // Enable saving new CAN address.

//--- WS2812 RGB LED ---//
CRGB rgbLeds[RGB_LED_NUM];                                                  // Define LED struct.
CircularBuffer<RGBValues, 5> RGBColorBuffer;                                // Queue for RGB values.

//--- Button ---//
PushButton Button(200, 500, 70);                                            // Object to handle pushbutton events.
CircularBuffer<uint8_t, 10> buttonEventBuffer;                              // Store button events.

//--- MP3 player ---//
DFPlayer MP3Player(DFP_RX, DFP_TX, DFP_EN, DFP_BUSY);                       // Object to handle MP3 player device.

//--- Setup section ---//
void setup() {
  wdt_disable();                                                            // Disable WDT (Watchdog timer).
  Serial.begin(115200);                                                     // Open serial port with the given baudrate.

  pinMode(LED, OUTPUT);                                                     // LED pin -> output.
  pinMode(CAN_INT, INPUT_PULLUP);                                           // CAN_INT pin -> input with pullup.
  pinMode(BUTTON, INPUT_PULLUP);                                            // Button pin -> input with pullup.

  LED_H;                                                                    // Turn on LED.
  Serial.println(F("*************************"));
  Serial.println(F("Starting..."));                                         // Serial debug print.
  Serial.print(F("FW: "));
  Serial.println(SW_VERSION);

  FastLED.addLeds<CHIP_SET, RGB_PIN, COLOR_CODE>(rgbLeds, RGB_LED_NUM);     // Setup LED strip.
  FastLED.clear();                                                          // Clear LEDs
  FastLED.show();                                                           // and show it.

  LoadFromEEPROM();                                                         // Loads stored data from EEPROM.

  Serial.print(F("CAN"));                                                   // Serial debug print.
  CAN.setClockFrequency(8e6);                                               // SPI CAN controller runs from 8MHz crystal.
  if(CAN.begin(500E3) == false) {                                           // Set CAN speed to 500Kb/s.
    Serial.println(ERR_STATE);                                              // If can't init CAN controller, print ERROR.
  }
  else {
    Serial.println(OK_STATE);                                               // If init ok, print OK.
  }
  CAN.filterExtended(dataToExtId(0, 0, settings.canAddress), CAN_MASK);     // Setup extended CAN ID filtering.

  ///////////////////////////////////////////////
  MP3Player.attachRGBController(addToRGBQueue);
  MP3Player.volume(15);
  MP3Player.play(1);
  MP3Player.play(2);
  MP3Player.play(1);
  MP3Player.play(3);
  MP3Player.play(1);
  ///////////////////////////////////////////////

  analogReference(DEFAULT);                                                 // Setup analog reference to 5V.
  attachInterrupt(digitalPinToInterrupt(CAN_INT), canIrqHandler, FALLING);  // Setup interrupt pin for CAN controller.

  canCommandBuffer.put(canCmd::NODE_CMD_IDLE);                              // Set state machine default command.
  canCallbackBuffer.put(canCb::NODE_CB_RESTARTED);                          // Set callback state as restarted.
  Serial.println(F("*************************"));                           // Debug prints.
  Serial.println("Loop starting...");
  wdt_enable(WDTO_30MS);                                                    // Enable WDT with a timeout of 1 seconds.
  LED_L;                                                                    // Turn off LED.
}

void loop() {

  //--- Maintenance ---//
  uint32_t cycleTimer = millis();                                           // Save millis value for loop cost calculation.

  //--- Button press handling ---//
  uint8_t buttonState = Button.buttonCheck(millis(), digitalRead(BUTTON));  // Check button actual state.
  if( buttonState > 0 ) {                                                   // Filter unvalid states.
    canCallbackBuffer.put(canCb::NODE_CB_BUTTON_EVENT);                     // Store the CAN callback.
    buttonEventBuffer.put(buttonState);                                     // Store the button event.
    canCommandBuffer.put(canCmd::NODE_CMD_GET_BUTTON_EVENT);                // Put command to queue.
    Serial.print(F("Button event: "));                                      // Debug prints.
    Serial.println(buttonState);
  }

  //--- Processing CAN messages ---//
  uint8_t recvData[8] = { 0 };                                              // We will store the message in this variable.
  uint8_t canMsg[ 8 ] = { 0 };                                              // Response message data.
  uint16_t masterAddress = DEFAULT_MASTER_ADDRESS;                          // Master CAN address.
  canCmd cmdExec = canCmd::NODE_CMD_IDLE;                                   // Execution command for the state machine.
  uint32_t extendedIdOut = 0;                                               // Extended CAN ID to send.
  bool enableCanAnswer = true;                                              // Disable CAN answer, for standard addresses.

  if(canProcess > 0) {                                                      // If the CAN controller interrupted.
    canProcess--;                                                           // Lower value.
    uint8_t recvSize = CAN.parsePacket();                                   // Process CAN message length.
    uint32_t extendedIdIn = 0;                                              // Received CAN packet extended ID.
    uint16_t localAddress = 0;                                              // Local CAN address.
    uint16_t cmd = 0;                                                       // CAN command.

    if((CAN.available() > 0) && (recvSize > 0)) {
      CAN.readBytes(recvData, sizeof(recvData));                            // Read the message.
    }

    if(CAN.packetExtended() == false) {                                     // Handle standard messages as broadcast messages.
      canCommandBuffer.put(static_cast<canCmd>(CAN.packetId()));            // Put CAN ID in command buffer as command.
      enableCanAnswer = false;                                              // Disable answer for broadcast messages.
      return;                                                               // Terminate processing.
    }

    extendedIdIn = CAN.packetId();                                          // Set CAN packet extended ID.
    extIdToData(extendedIdIn, &masterAddress, &cmd, &localAddress);         // Extract data from ID.

    if(settings.canAddress != localAddress) {                               // Software CAN ID filter. (HW doesn't work properly.)
      Serial.print(F("SW filter rejected ID: "));                           // Debug print of dropped package.
      Serial.println(extendedIdIn);
      return;
    }

    canCommandBuffer.put(static_cast<canCmd>(cmd));                         // Put received command to queue.
    pingTimer = millis();                                                   // Ping timer reload.
    LED_L;                                                                  // LED off.
  }  // End of if state (processing CAN).


  if(canCommandBuffer.isEmpty() == false) {                                 // If available, get next state from
    cmdExec = canCommandBuffer.pop();                                       // execution queue.
    extendedIdOut = dataToExtId(settings.canAddress,
      static_cast<uint8_t>(cmdExec), masterAddress);                        // Make extended CAN ID to send.
  }

  switch(cmdExec) {                                                         // Send the command in the switch.

    case canCmd::NODE_CMD_IDLE: {                                           // Idle state.

    } break;

    case canCmd::NODE_CMD_PING: {                                           // Ping command.
      if(canCallbackBuffer.isEmpty() == false) {                            // Check if callback needed.
        canMsg[0] = static_cast<uint8_t>(canCallbackBuffer.pop());          // Send the callback type to the master.
      }
      if(enableCanAnswer == true) {                                         // Check if answering is enabled.
        sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));             // Send answer.
      }
    } break;

    case canCmd::NODE_CMD_RESET: {                                          // Reset MCU.
      if(enableCanAnswer == true) {                                         // Check if answering is enabled.
        sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));             // Send answer.
      }
      resetCMD();                                                           // Call reset function.
    } break;

    case canCmd::NODE_CMD_GET_FW_VERSION: {                                 // Send firmware version to master.
      memcpy(canMsg, SW_VERSION, sizeof(SW_VERSION));
      if(enableCanAnswer == true) {                                         // Check if answering is enabled.
        sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));             // Send answer.
      }
    } break;

    case canCmd::NODE_CMD_SETADDRESS: {                                     // Set new CAN address. Response: used address.
      uint16_t newAddress = (uint16_t)(recvData[0] | (recvData[1] << 8));   // 0.->address lowbyte, 1.->address highbyte.
      newAddress &= 0x3FF;                                                  // Can't be more than 1023.
      if(newAddress != settings.canAddress) {                               // Check if new address is equal to old or not.
        saveNewAddress = true;                                              // Enable saving.
        addToRGBQueue(0, 200, 0);                                           // Turn on RGB LED as green.
        Serial.println(F("Wait for button press to save new CAN address!"));  // Debug print.
      }
      else {
        Serial.println(F("Address already used!"));                         // Print if address is already used.
      }
      if(enableCanAnswer == true) {                                         // Check if answering is enabled.
        sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));             // Send answer.
      }
    } break;

    case canCmd::NODE_CMD_RGB_LED: {
      addToRGBQueue(recvData[0], recvData[1], recvData[2]);                 // Add color values to queue.
      if(enableCanAnswer == true) {                                         // Check if answering is enabled.
        sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));             // Send answer.
      }
    } break;

    case canCmd::NODE_CMD_GET_BUTTON_EVENT: {
      if(buttonEventBuffer.isEmpty() == false) {                            // Check if callback needed.
        canMsg[0] = buttonEventBuffer.pop();                                // Send button event.
      }
      if(enableCanAnswer == true) {                                         // Check if answering is enabled.
        sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));             // Send answer.
      }
    } break;

    case canCmd::NODE_CMD_PLAY_MP3: {
      MP3Player.play(recvData[0]);                                          // Play selected song.
      if(enableCanAnswer == true) {                                         // Check if answering is enabled.
        sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));             // Send answer.
      }
    } break;

    default: {                                                              // Default case.
      Serial.println(F("Command is unhandled"));                            // If somehow program reach it, print it.
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
  if((millis() - pingTimer ) >= pingTime) {                                 // Check if ping timer is expired.
    LED_H;                                                                  // If yes, turn on the LED.
  }

  //--- Handling MP3 player ---//
  MP3Player.spin();                                                         // Take care of playing queue.

  //--- Maintenance ---//
  if(saveNewAddress == true) {                                              // Check if saving is enabled.
    if(millis() - canAddressSaveTimer >= canAddressSaveTime) {              // Check saving timeout timer.
      saveNewAddress = false;                                               // Disable saving.
      Serial.println(F("Address saving timeout!"));                         // Debug print.
    }
    if(buttonState == 3) {
      uint16_t newAddress = (uint16_t)(recvData[0] | (recvData[1] << 8));   // 0.->address lowbyte, 1.->address highbyte.
      newAddress &= 0x3FF;                                                  // Can't be more than 1023.
      settings.isValid = EEPROM_VALID;                                      // Setup validity flag to struct.
      settings.canAddress = newAddress;                                     // Setup new can address to struct.
      if(SaveToEEPROM() == false) {                                         // Save struct to EEPROM.
        LoadFromEEPROM();                                                   // If not success, load old settings.
      }
      CAN.filterExtended(dataToExtId(0, 0, settings.canAddress), CAN_MASK); // Setup filter for new CAN address.
      saveNewAddress = false;                                               // Disable saving.
      addToRGBQueue(0, 0, 0);                                               // Turn off RGB LED.
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

void addToRGBQueue(uint8_t red, uint8_t green, uint8_t blue) {
  RGBValues RGBColor;
  RGBColor.red = red;                                                   // Set color values.
  RGBColor.green = green;
  RGBColor.blue = blue;
  RGBColorBuffer.put(RGBColor);                                         // Add to queue.
}

void sendCanResponse(uint32_t extId, uint8_t data[], uint8_t size) {
  CAN.beginExtendedPacket(extId);                                       // Set extended ID.
  CAN.write(data, size);                                                // Set data.
  CAN.endPacket();                                                      // Send packet.
}

void resetCMD(void) {
  Serial.println("Restarting...");
  Serial.flush();                                                       // Sends out data from serial buffer, before reset.
  resetFunc();                                                          // Call reset function.
}

void LoadFromEEPROM(void) {
  EEPROM.get(0, settings);                                // Reading data from EEPROM to settings struct.
  if(settings.isValid == EEPROM_VALID) {                  // If can address is valid, use it.
    Serial.print(SAVED_STATE);                            // Print saved mark.
  }
  else {
    settings.canAddress = DEFAULT_LOCAL_ADDRESS;          // If not, use default CAN address.
    Serial.print(DEFAULT_STATE);                          // Print default mark.
  }
  Serial.print(F("CAN address: "));                       // Print CAN address.
  Serial.println(settings.canAddress);
}

bool SaveToEEPROM(void) {
  const uint16_t cooldownTime = 10000;                    // EEPROM write cooldown time.
  static uint32_t cooldownTimer = 0;                      // Cooldown timer.
  static bool firstRun = true;                            // Allow first change without cooldown.
  if((millis() - cooldownTimer > cooldownTime) || (firstRun == true)) {  // Timer expiration check.
    cooldownTimer = millis();                             // Reload timer.
    EEPROM.put(0, settings);                              // Save settings struct to EEPROM.
    Serial.println(F("Saved!"));                          // Debug print.
    firstRun = false;
    return true;                                          // Return true.
  }
  else {                                                  // If EEPROM write is on cooldown,
    Serial.println(F("Not saved! EEPROM cooldown!"));     // print it
    return false;                                         // and return false.
  }
}

uint32_t dataToExtId(uint16_t from, uint16_t cmd, uint16_t to) {
  from &= 0x3FF;                                          // 10bit
  cmd &= 0x1FF;                                           // 9bit
  to &= 0x3FF;                                            // 10bit
  return (uint32_t)(((uint32_t)from << 0) | ((uint32_t)cmd << 10) | ((uint32_t)to << 19));
}

void extIdToData(uint32_t extId, uint16_t* from, uint16_t* cmd, uint16_t* to) {
  extId &= 0x1fffffff;                                    // 29bit
  *from = (uint16_t)((extId >> 0) & 0x3FF);               // 10bit
  *cmd = (uint16_t)((extId >> 10) & 0x1FF);               // 9bit
  *to = (uint16_t)((extId >> 19) & 0x3FF);                // 10bit
}

void canIrqHandler(void) {                                // CAN controller iterrupt rutin handler.
  canProcess++;                                           // Count incoming packets.
}
