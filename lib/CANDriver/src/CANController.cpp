#include "CANController.h"

CANController::CANController() :
  onReceiveCb(nullptr),
  packetBegun(false),
  txId(-1),
  txExtended(false),
  txRtr(false),
  txDlc(0),
  txLength(0U),
  txData{},
  rxId(-1),
  rxExtended(false),
  rxRtr(false),
  rxDlc(0U),
  rxLength(0U),
  rxIndex(0U),
  rxData{}
{
  setTimeout(0);
}

uint8_t CANController::begin(uint32_t /*baudRate*/) {
  packetBegun = false;
  txId = -1;
  txRtr = false;
  txDlc = 0;
  txLength = 0U;

  rxId = -1;
  rxRtr = false;
  rxDlc = 0U;
  rxLength = 0U;
  rxIndex = 0U;

  return 1U;
}

void CANController::end() {}

uint8_t CANController::beginPacket(uint16_t id, int8_t dlc, bool rtr) {
  if(id > 0x7FFU) { return 0U; }
  if(dlc > 8) { return 0U; }

  packetBegun = true;
  txId = static_cast<int32_t>(id);
  txExtended = false;
  txRtr = rtr;
  txDlc = dlc;
  txLength = 0U;

  memset(txData, 0x00U, sizeof(txData));

  return 1U;
}

uint8_t CANController::beginExtendedPacket(uint32_t id, int8_t dlc, bool rtr) {
  if(id > 0x1FFFFFFFU) { return 0U; }
  if(dlc > 8) { return 0U; }

  packetBegun = true;
  txId = static_cast<int32_t>(id);
  txExtended = true;
  txRtr = rtr;
  txDlc = dlc;
  txLength = 0U;

  memset(txData, 0x00U, sizeof(txData));

  return 1U;
}

uint8_t CANController::endPacket() {
  if(!packetBegun) { return 0U; }
  packetBegun = false;

  if(txDlc >= 0) {
    txLength = static_cast<uint8_t>(txDlc); // txDlc is 0..8 here
  }

  return 1U;
}

uint8_t CANController::parsePacket() { return 0U; }

int32_t CANController::packetId() const { return rxId; }
bool CANController::packetExtended() const { return rxExtended; }
bool CANController::packetRtr() const { return rxRtr; }
uint8_t CANController::packetDlc() const { return rxDlc; }

size_t CANController::write(uint8_t b) {
  return write(&b, sizeof(b));
}

size_t CANController::write(const uint8_t* buffer, size_t size) {
  if(!packetBegun) { return 0U; }

  const size_t spaceAvailable = sizeof(txData) - txLength;
  if(size > spaceAvailable) {
    size = spaceAvailable;
  }

  memcpy(&txData[txLength], buffer, size);
  txLength += static_cast<uint8_t>(size);

  return size;
}

int CANController::available() { return static_cast<int>(rxLength) - rxIndex; }

int CANController::read() {
  if(available() == 0) { return -1; }
  return rxData[rxIndex++];
}

int CANController::peek() {
  if(available() == 0) { return -1; }
  return rxData[rxIndex];
}

void CANController::flush() {}

void CANController::onReceive(void(*callback)(int)) {
  onReceiveCb = callback;
}

uint8_t CANController::filter(uint16_t /*id*/, uint16_t /*mask*/) { return 0U; }
uint8_t CANController::filterExtended(uint32_t /*id*/, uint32_t /*mask*/) { return 0U; }
uint8_t CANController::observe() { return 0U; }
uint8_t CANController::loopback() { return 0U; }
uint8_t CANController::sleep() { return 0U; }
uint8_t CANController::wakeup() { return 0U; }
