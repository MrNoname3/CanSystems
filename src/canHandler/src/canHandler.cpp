#include "canHandler.hpp"
#include <CAN.h>                              /// CAN controller library.
#include <avr/wdt.h>                          /// Watchdog timer library.
#include <avr/boot.h>                         /// Reading fuses.

CircularBuffer<CanHandler::CanFrame, CanHandler::rxBufferSize> CanHandler::rxBuffer;

CanHandler::CanHandler(HardwareSerial& serial, uint8_t canCsPin, uint8_t canIntPin, uint8_t ledPin, uint8_t flashCsPin) :
  serialPort(serial),
  localCanId(0),
  eepromHandler(&localCanId),
  ledPin(ledPin),
  flash(flashCsPin, flashJedecId)
{
  wdt_enable(WDTO_2S);                        // Enable WDT timer.
  CAN.setPins(canCsPin, canIntPin);
  pinMode(ledPin, OUTPUT);
}

void CanHandler::begin(uint32_t canBaud) {
  const bool beginResult = beginSimple(canBaud);
  if(!beginResult) {
    serialPort.println(F("Init ERROR!"));
    restartMCU();
  }
}

bool CanHandler::beginSimple(uint32_t canBaud) {
  static constexpr uint16_t fwVersion = GIT_COMMIT_COUNT;
  static constexpr uint32_t gitHash = GIT_COMMIT_HASH;
  {
    static constexpr uint32_t cppVersion = __cplusplus;
    const char* SPACER = "|";
    serialPort.print(F("CPP: "));
    serialPort.println(cppVersion);
    serialPort.print(F("FW: "));
    serialPort.println(fwVersion);
    serialPort.print(F("GIT: "));
    serialPort.println(gitHash, HEX);
    serialPort.print(F("Fuses: "));
    serialPort.print(boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS), HEX);
    serialPort.print(SPACER);
    serialPort.print(boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS), HEX);
    serialPort.print(SPACER);
    serialPort.print(boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS), HEX);
    serialPort.print(SPACER);
    serialPort.println(boot_lock_fuse_bits_get(GET_LOCK_BITS), HEX);
  }
  {
    serialPort.print(F("Address: "));
    const bool eepromDataValid = eepromHandler.load();
    eepromDataValid ? serialPort.println(localCanId) : serialPort.println(ERR_STATE);
    if(!eepromDataValid) { return false; }
  }
  {
    serialPort.print(F("CAN: "));
    CAN.setClockFrequency(8E6);                     // SPI CAN controller runs from 8MHz crystal.
    CAN.setSPIFrequency(4E6);
    const bool canBeginResult = CAN.begin(canBaud) == 1;
    canBeginResult ? serialPort.println(OK_STATE) : serialPort.println(ERR_STATE);
    if(!canBeginResult) { return false; }
  }
  { // Calculate the mask to ignore the upper bits of the extended CAN ID and only consider the lower 10 bits.
    const uint16_t deviceAddress = localCanId;
    const uint32_t mask = 0x7FF;                    // Mask for lower 10 bits (0b1111111111).
    const uint32_t id = deviceAddress & mask;       // Calculate the ID using the device's local address.
    serialPort.print(F("Filter: "));
    const bool setFilterResult = CAN.filterExtended(id, mask) == 1;
    setFilterResult ? serialPort.println(OK_STATE) : serialPort.println(ERR_STATE);
    if(!setFilterResult) { return false; }
    CAN.onReceive(rxInterrupt);
  }
  {
    static constexpr uint8_t versionInfo[8] = {
      static_cast<uint8_t>((fwVersion >> 0) & 0xFF),
      static_cast<uint8_t>((fwVersion >> 8) & 0xFF),
      static_cast<uint8_t>((gitHash >> 0) & 0xFF),
      static_cast<uint8_t>((gitHash >> 8) & 0xFF),
      static_cast<uint8_t>((gitHash >> 16) & 0xFF),
      static_cast<uint8_t>((gitHash >> 24) & 0xFF),
      0,
      0
    };
    const bool sendResult = send(CanCmd::RESTART, versionInfo);
    if(!sendResult) { return false; }
  }
  {
    serialPort.print(F("FLASH: "));
    const bool flashInitResult = flash.initialize();
    flashInitResult ? serialPort.println(OK_STATE) : serialPort.println(ERR_STATE);
    if(!flashInitResult) { return false; }
  }
  wdt_reset();                                                    // Reset the watchdog timer.
  return true;
}

void CanHandler::loop() {
  const bool loopResult = loopSimple();
  if(!loopResult) {
    serialPort.println(F("Loop ERROR!"));
    restartMCU();
  }
}

bool CanHandler::loopSimple() {
  static uint32_t pingTimer = millis();
  if(!rxBuffer.isEmpty()) {
    pingTimer = millis();                                         // Ping timer reload.
    ledOff();
    CanFrame canFrame = rxBuffer.pop();
    switch(static_cast<uint16_t>(canFrame.cmd)) {
      case static_cast<uint16_t>(CanCmd::PING): { send(CanCmd::PING); } break;
      case static_cast<uint16_t>(CanCmd::RESTART): { restartMCU(); } break;
    }
  }
  if(millis() - pingTimer >= pingTime) {                          // Check if ping timer is expired.
    ledOn();                                                      // If yes, turn on the LED.
  }
  static uint32_t cycleTimer = millis();
  static uint16_t cycleTime = 2U;
  const uint16_t actulaCycleTime = millis() - cycleTimer;
  cycleTimer = millis();
  if(actulaCycleTime > cycleTime) {
    cycleTime = actulaCycleTime;
    serialPort.print(F("Max cycle time: "));
    serialPort.println(cycleTime);
  }
  wdt_reset();                                                    // Reset the watchdog timer.
  return true;
}

bool CanHandler::send(uint16_t command, const uint8_t (&data)[8]) const {
  const uint32_t extId =
    ((static_cast<uint32_t>(masterCanId) & 0x3FF) << 0) |
    ((static_cast<uint32_t>(command) & 0x1FF) << 10) |
    ((static_cast<uint32_t>(localCanId) & 0x3FF) << 19);
  const bool beginPacketResult = CAN.beginExtendedPacket(extId) > 0;
  if(!beginPacketResult) { return false; }
  const bool packetWriteResult = CAN.write(data, sizeof(data)) > 0;
  if(!packetWriteResult) { return false; }
  const bool endPacketResult = CAN.endPacket() > 0;
  if(!endPacketResult) { return false; }
  return true;
}

bool CanHandler::send(CanCmd command, const uint8_t (&data)[8]) const {
  return send(static_cast<uint16_t>(command), data);
}

bool CanHandler::send(uint16_t command) const {
  uint8_t data[8] = { 0 };
  return send(command, data);
}

bool CanHandler::send(CanCmd command) const {
  return send(static_cast<uint16_t>(command));
}

void CanHandler::rxInterrupt(int packetsNum) {
  if(packetsNum <= 0) { return; }
  if(!CAN.packetExtended()) { return; }
  CanFrame rxCanData;
  rxCanData.extId = CAN.packetId();
  if(!CAN.packetRtr()) {
    const uint8_t canDataDlc = static_cast<uint8_t>(CAN.packetDlc());
    const uint8_t bytesReaded = static_cast<uint8_t>(CAN.readBytes(rxCanData.data, canDataDlc));
    if(canDataDlc != bytesReaded) { return; }
  }
  if(!rxBuffer.isFull()) { rxBuffer.put(rxCanData); }
}

void CanHandler::ledOn() { digitalWrite(ledPin, HIGH); }

void CanHandler::ledOff() { digitalWrite(ledPin, LOW); }

void CanHandler::ledToggle() { digitalWrite(ledPin, !digitalRead(ledPin)); }

void CanHandler::restartMCU() {
  serialPort.println(F("Restarting..."));
  serialPort.flush();                                 // Sends out data from serial buffer, before reset.
  wdt_enable(WDTO_15MS);                              // Setup watchdog timer.
  while(true) { };                                    // Let the WDT restart the MCU.
}