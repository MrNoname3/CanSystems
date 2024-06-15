//--- Headers ---//
#include <Arduino.h>                                                /// Arduino libraries header.
#include <CAN.h>                                                    /// SPI CAN controller library.
#include <EEPROM.h>                                                 /// EEPROM access library.
#include <avr/wdt.h>                                                /// Watchdog timer library.
#include <FastLED.h>                                                /// WS2812 LED driver library.
#include "pushButtonHandler/src/pushButtonHandler.hpp"              /// Pushbutton events library.
#include "CircularBuffer/src/CircularBuffer.hpp"                    /// Circular buffer class.
//#include "MillisTimer.hpp"                    /// Millisec timer class.

//--- Constants ---//
#define SW_VERSION "V1.0.0"                   // Actual software version.
#define EEPROM_VALID 231                      // EEPROM validity check.
#define DEFAULT_LOCAL_ADDRESS 444             // Node default address if no saved available.
#define DEFAULT_MASTER_ADDRESS 10             // Default CAN master address.
#define CAN_MASK 0x1FF80000                   // CAN extended ID mask.
#define OK_STATE " [ OK ]"                    // OK status.
#define ERR_STATE " [ ERR ]"                  // Error status.
#define SAVED_STATE "[S] "                    // Saved data mark.
#define DEFAULT_STATE "[D] "                  // Default data mark.

#define RGB_LED_NUM   1                       // 1pcs LED.
#define CHIP_SET      WS2812B                 // Types of RGB LEDs.
#define COLOR_CODE    GRB                     // Sequence of colors in data stream.

#define RGB_PIN       7                       // LED DATA PIN
#define LED           4                       // Pin of the LED.
#define CAN_INT       2                       // Interrupt pin of the SPI CAN controller.
#define BUTTON        8                       // Pushbutton pin.
#define DFP_EN        9                       // DFPlayer switch pin.
#define DFP_BUSY      3                       // DFPlayer busy pin.
#define DFP_TX        5                       // DFPlayer serial RX pin.
#define DFP_RX        6                       // DFPlayer serial TX pin.
#define CHARGE_PIN    17                      // Capacitor charge enable pin. (A3)

#define LED_T (PORTD ^=  (1 << PORTD4))       // LED pin toggle.
#define LED_H (PORTD |=  (1 << PORTD4))       // LED pin high.
#define LED_L (PORTD &= ~(1 << PORTD4))       // LED pin low.
#define NOP   __asm__("nop\n\t");             // 1 CPU cycle delay.

//--- Structs ---//
struct Settings {                             // The struct of the settings in the EEPROM.
  uint8_t isValid = 0;                        // Variable of address data validity.
  uint16_t canAddress = DEFAULT_LOCAL_ADDRESS;  // Variable of CAN address.
};

/// @brief Color values for RGB LED.
struct RGBValues {
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
};

//--- Enums ---//
/// @brief Base command list for nodes.
enum class canCmdB : uint16_t {
  BCMD_IDLE = 0,                              // Idle state.
  BCMD_PING,                                  // Ping command.
  BCMD_RESET,                                 // Node reset command.
  BCMD_GET_FW_VERSION,                        // Firmware version command.
  BCMD_SETADDRESS,                            // CAN address setup.
  BCMD_RGB_LED,                               // Set WS2812 RGB LED color.
  BCMD_GET_BUTTON_EVENT,                      // Get button event type.

  BCMD_LAST_ELEMENT                           // Last element of enum!
};

/// @brief Node type specific extended command list.
enum class canCmdE : uint16_t {
  ECMD_MOISTURE = static_cast<uint16_t>(canCmdB::BCMD_LAST_ELEMENT),  // Get moisture data.
  ECMD_LDR,                                   // Get ldr data.
  ECMD_IRRIGATION,                            // Plant irrigation.

  ECMD_LAST_ELEMENT                           // Last element of enum!
};

/// @brief Callback types.
enum class canCb : uint16_t {
  NODE_CB_IDLE = 0,                           // No event state.
  NODE_CB_RESTARTED,                          // Node restarted.
  //NODE_CB_ERROR,                              // Error type callback.

  NODE_CB_LAST_ELEMENT                        // Last element of enum!
};

/// @brief Error types.
enum class errorTypes : uint8_t {
  ERR_I2C_READ_TIMEOUT = 0,                   // I2C read timeout.
  ERR_I2C_SENSOR_INIT,                        // I2C sensor init failed.
  ERR_CAN_INIT,                               // CAN init failed.
  ERR_CAN_ID_SET,                             // CAN ID setup method failed.
  ERR_CAN_DATA_WRITE,                         // CAN data write method failed.
  ERR_CAN_ENDPACKET,                          // CAN endpacket method failed.
  ERR_UNHANDLED_COMMAND,                      // Unhandled command in state machine.

  LAST_ELEMENT                                // Last element of enum!
};

/// @brief The states of irrigation system.
enum class irrigationState : uint8_t {
  IRR_IDLE = 1,                               // Idle state.
  IRR_ENABLE,                                 // Irrigation enabled.
  IRR_SPIN_UP,                                // Spin up the pumps with PWM.
  IRR_IRRIGATION,                             // Start irrigation with given time.

  IRR_LAST_ELEMENT                            // Last element of enum!
};

/// Analog readings.
///
/// @brief This function reads the analog sensors in the loop.
void AnalogReads(void);

/// @brief Put the given data to RGB LED queue.
/// @param red Value of red color: 0-255.
/// @param green Value of green color: 0-255.
/// @param blue Value of blue color: 0-255.
void addToRGBQueue(uint8_t red, uint8_t green, uint8_t blue);

/// @brief Disable answer to standard CAN IDs,
/// because it is a broadcast message for all nodes.
/// @param extId Extended CAN ID.
/// @param data Data array to be send.
/// @param size Size of data array. Maximum 8 byte.
void sendCanResponse(uint32_t extId, const uint8_t data[], uint8_t size);

/// @brief Reset the MCU.
void resetCMD();

/// @brief Load saved data from EEPROM to settings structure.
void LoadFromEEPROM();

/// @brief Save data to EEPROM from the settings structure.
bool SaveToEEPROM();

/// @brief Converts the given data to extended CAN ID.
/// @param from Sender address.
/// @param cmd Command from sender.
/// @param to_ Target address.
/// @return Returns with the CAN address.
uint32_t dataToExtId(uint16_t from, uint16_t cmd, uint16_t to_);

/// @brief Converts the given extended CAN ID to addresses and command variable.
/// @param extId Extended CAN ID.
/// @param from Pointer to sender address variable.
/// @param cmd Pointer to command variable.
/// @param to_ Pointer to target address variable.
void extIdToData(uint32_t extId, uint16_t* from, uint16_t* cmd, uint16_t* to_);

/// @brief Handles the interrupts from the SPI CAN controller.
void canIrqHandler();

/// @brief Reset the microcontroller.
void (*resetFunc)() = nullptr;

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
PushButton Button(200, 500, 70, false);                                     // Object to handle pushbutton events.
CircularBuffer<uint8_t, 10> buttonEventBuffer;                              // Store button events.

//--- Analog section ---//
#define MOIST_NUM 4                                                         // Number of moisture sensors.
const uint8_t moisturePins[MOIST_NUM] = { A0, A1, A2, A3 };                 // Pins of moisture sensors.
float moistureValue[MOIST_NUM] = { 0.0 };                                   // Moisture values.

#define LDR_NUM 4                                                           // Number of ldr sensors.
const uint8_t ldrPins[LDR_NUM] = { A4, A5, A6, A7 };                        // Pins of ldr sensors.
float ldrValue[LDR_NUM] = { 0.0 };                                          // Ldr values.

//--- Irrigation ---//
#define PUMP_NUM 4                                                          // Number of water pumps.
const uint8_t pumpPins[PUMP_NUM] = { 9, 6, 5, 3 };                          // Pins of pumps.
uint32_t irrigationTimer[PUMP_NUM] = { 0 };                                 // Timer for irrigation.
uint16_t irrigationTime[PUMP_NUM] = { 0 };                                  // Stores irrigation times.
uint32_t pumpSpinUpTimer[PUMP_NUM] = { 0 };                                 // Timer for pump spin up.
uint8_t pumpPwm[PUMP_NUM] = { 0 };                                          // Stores the pumps pwm values for spinning up.
irrigationState irrState[PUMP_NUM] = { irrigationState::IRR_IDLE };         // Irrigation state variable.

//--- Setup section ---//
void setup() {
  wdt_disable();                                                            // Disable WDT (Watchdog timer).
  Serial.begin(115200);                                                     // Open serial port with the given baudrate.

  pinMode(LED, OUTPUT);                                                     // LED pin -> output.
  pinMode(CAN_INT, INPUT_PULLUP);                                           // CAN_INT pin -> input with pullup.
  pinMode(BUTTON, INPUT_PULLUP);                                            // Button pin -> input with pullup.
  for(uint8_t i = 0; i < PUMP_NUM; i++) {
    pinMode(pumpPins[i], OUTPUT);                                           // Pump pins -> output.
    delay(1);                                                               // Little delay.
    digitalWrite(pumpPins[i], LOW);                                         // Set pumps to off.
  }

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

  Serial.println(F("*************************"));                           // Debug prints.
  Serial.println(F("Loop starting..."));
  wdt_enable(WDTO_30MS);                                                    // Enable WDT timer.
  LED_L;                                                                    // Turn off LED.
}

void loop() {

  //--- Maintenance ---//
  uint32_t cycleTimer = millis();                                           // Save millis value for loop cost calculation.

  //--- Analog reads ---//
  AnalogReads();                                                            // Read analog sensors.

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

    case static_cast<uint16_t>(canCmdE::ECMD_MOISTURE): {                   // Send moisture values.
      for(uint8_t i = 0; i < MOIST_NUM; i++) {                              // Fill moisture values to message array.
        canMsg[i] = static_cast<uint8_t>(round(moistureValue[i]));
      }
      sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));               // Send answer.
    } break;

    case static_cast<uint16_t>(canCmdE::ECMD_LDR): {                        // Send ldr values.
      for(uint8_t i = 0; i < LDR_NUM; i++) {                                // Fill ldr values to message array.
        canMsg[i] = static_cast<uint8_t>(round(ldrValue[i]));
      }
      sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));               // Send answer.
    } break;

    case static_cast<uint16_t>(canCmdE::ECMD_IRRIGATION): {                 // This state enables the irrigation.
      uint8_t pump = recvData[0];                                           // Select the water pump.
      uint16_t irrigationTime_l = recvData[1] * 1000;                       // Irrigation time. We using seconds.
      if(pump < PUMP_NUM) {                                                 // Check pump number validity.
        sendCanResponse(extendedIdOut, canMsg, sizeof(canMsg));               // Send answer.
        irrState[pump] = irrigationState::IRR_ENABLE;                       // Enable irrigation for selected pump.
        irrigationTime[pump] = irrigationTime_l;                            // Setup irrigation time.
      }
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

  //--- Handling irrigation states ---//
  for(uint8_t i = 0; i < PUMP_NUM; i++) {                               // Check all irrigation system.
    switch(irrState[i]) {                                               // Check irrigation state.

      case irrigationState::IRR_IDLE: {                                 // Do nothing in IDLE state.
        digitalWrite(pumpPins[i], LOW);                                 // Turn pump pin off. (Just to make sure.)
      } break;

      case irrigationState::IRR_SPIN_UP: {                              // SPIN_UP state spins up the pumps.
        if(millis() - pumpSpinUpTimer[i] >= 30) {                       // Every 30ms
          pumpPwm[i] = pumpPwm[i] + 5;                                  // add 5 to pump PWM value.
          analogWrite(pumpPins[i], pumpPwm[i]);                         // Set up the PWM for the pump.
          if(pumpPwm[i] == 255) {                                       // Check if the PWM value reaches it's maximum value.
            irrState[i] = irrigationState::IRR_IRRIGATION;              // If yes, we can go to next state.
            irrigationTimer[i] = millis();                              // Irrigation timer reload.
          }
          pumpSpinUpTimer[i] = millis();                                // Spin up timer reload.
        }
      } break;

      case irrigationState::IRR_IRRIGATION: {
        if(millis() - irrigationTimer[i] >= irrigationTime[i]) {        // Check if irrigation timer is expired.
          pumpPwm[i] = 0;                                               // If expired, we set the PWM value to 0.
          analogWrite(pumpPins[i], pumpPwm[i]);                         // Then set up the PWM for the pump.
          irrState[i] = irrigationState::IRR_IDLE;                      // After irrigation complet, we can go back to IDLE state.
        }
      } break;

      default: {                                                        // Deafult state is not reachable in theory in our case.
        irrState[i] = irrigationState::IRR_IDLE;                        // If somehow the program reaches, we set the state to IDLE.
      }

    }  // End of switch.
  }  // End of for loop.

  //--- Maintenance ---//
  if(saveNewAddress) {                                                      // Check if saving is enabled.
    if(millis() - canAddressSaveTimer >= canAddressSaveTime) {              // Check saving timeout timer.
      saveNewAddress = false;                                               // Disable saving.
      Serial.println(F("Address saving timeout!"));                         // Debug print.
      addToRGBQueue(0, 0, 0);                                               // Turn off RGB LED.
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

void AnalogReads(void) {
  const float adcInputFilterAlpha = 0.1;                                // Complementer filter constant.
  uint16_t rawMoistureValue[MOIST_NUM] = { 0 };                         // Raw analog moisture values.
  uint8_t moistureValueTemp[MOIST_NUM] = { 0 };                         // Temporary moisture values.
  uint16_t rawLdrValue[LDR_NUM] = { 0 };                                // Raw analog ldr values.
  uint8_t ldrValueTemp[LDR_NUM] = { 0 };                                // Temporary ldr values.

  for(uint8_t i = 0; i < MOIST_NUM; i++) {                              // Read all analog moisture values.
    rawMoistureValue[i] = ((uint16_t)1023 - analogRead(moisturePins[i])); // Invert the readings to simplify handling.
    moistureValueTemp[i] = map(rawMoistureValue[i], 0, 1023, 0, 255);     // Map the values.
    // Complementer filter: DATA_filtered[new] = DATA_measured * Alpha + DATA_filtered[old] * ( 1 - Alpha )
    moistureValue[i] = (adcInputFilterAlpha * (float)moistureValueTemp[i]) + ( (float)1.0 - adcInputFilterAlpha ) * (float)moistureValue[i];
  }

  for(uint8_t i = 0; i < LDR_NUM; i++) {                                // Read all analog ldr values.
    rawLdrValue[i] = analogRead(ldrPins[i]);                            // Readings.
    ldrValueTemp[i] = map(rawLdrValue[i], 0, 1023, 0, 255);             // Map the values.
    // Complementer filter: DATA_filtered[new] = DATA_measured * Alpha + DATA_filtered[old] * ( 1 - Alpha )
    ldrValue[i] = (adcInputFilterAlpha * (float)ldrValueTemp[i]) + ( (float)1.0 - adcInputFilterAlpha ) * (float)ldrValue[i];
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