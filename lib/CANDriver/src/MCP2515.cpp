#if !defined(ARDUINO_ARCH_ESP32)

#include "MCP2515.h"

namespace {
  // clang-format off
  constexpr uint8_t regBfpCtrl   = 0x0CU;
  constexpr uint8_t regTxRtsCtrl = 0x0DU;
  constexpr uint8_t regCanCtrl   = 0x0FU;
  constexpr uint8_t regCnf3      = 0x28U;
  constexpr uint8_t regCnf2      = 0x29U;
  constexpr uint8_t regCnf1      = 0x2AU;
  constexpr uint8_t regCanInte   = 0x2BU;
  constexpr uint8_t regCanIntf   = 0x2CU;

  constexpr uint8_t flagIde   = 0x08U;
  constexpr uint8_t flagSrr   = 0x10U;
  constexpr uint8_t flagRtr   = 0x40U;
  constexpr uint8_t flagExide = 0x08U;
  constexpr uint8_t flagRxm0  = 0x20U;
  constexpr uint8_t flagRxm1  = 0x40U;
  // clang-format on

  constexpr uint8_t flagRxnIe(uint8_t n) { return static_cast<uint8_t>(0x01U << n); }
  constexpr uint8_t flagRxnIf(uint8_t n) { return static_cast<uint8_t>(0x01U << n); }
  constexpr uint8_t flagTxnIf(uint8_t n) { return static_cast<uint8_t>(0x04U << n); }

  constexpr uint8_t regRxFnSidh(uint8_t n) { return static_cast<uint8_t>(0x00U + n * 4U); }
  constexpr uint8_t regRxFnSidl(uint8_t n) { return static_cast<uint8_t>(0x01U + n * 4U); }
  constexpr uint8_t regRxFnEid8(uint8_t n) { return static_cast<uint8_t>(0x02U + n * 4U); }
  constexpr uint8_t regRxFnEid0(uint8_t n) { return static_cast<uint8_t>(0x03U + n * 4U); }

  constexpr uint8_t regRxMnSidh(uint8_t n) { return static_cast<uint8_t>(0x20U + n * 0x04U); }
  constexpr uint8_t regRxMnSidl(uint8_t n) { return static_cast<uint8_t>(0x21U + n * 0x04U); }
  constexpr uint8_t regRxMnEid8(uint8_t n) { return static_cast<uint8_t>(0x22U + n * 0x04U); }
  constexpr uint8_t regRxMnEid0(uint8_t n) { return static_cast<uint8_t>(0x23U + n * 0x04U); }

  constexpr uint8_t regTxBnCtrl(uint8_t n) { return static_cast<uint8_t>(0x30U + n * 0x10U); }
  constexpr uint8_t regTxBnSidh(uint8_t n) { return static_cast<uint8_t>(0x31U + n * 0x10U); }

  constexpr uint8_t regRxBnCtrl(uint8_t n) { return static_cast<uint8_t>(0x60U + n * 0x10U); }
  constexpr uint8_t regRxBnSidh(uint8_t n) { return static_cast<uint8_t>(0x61U + n * 0x10U); }
  constexpr uint8_t regRxBnD0(uint8_t n) { return static_cast<uint8_t>(0x66U + n * 0x10U); }
} // namespace

uint8_t MCP2515::begin(uint32_t baudRate) {
  if(CANController::begin(baudRate) != 1) { return 0U; }

  pinMode(csPin, OUTPUT);

  SPI.begin();

  reset();

  writeRegister(regCanCtrl, 0x80U);
  if(readRegister(regCanCtrl) != 0x80U) { return 0U; }

  struct CnfEntry {
    uint32_t clockFrequency;
    uint32_t baudRate;
    uint8_t cnf[3];
  };

  static constexpr CnfEntry cnfMapper[] = {
    // clang-format off
    {  8'000'000U, 1'000'000U, { 0x00U, 0x80U, 0x00U } },
    {  8'000'000U,   500'000U, { 0x00U, 0x90U, 0x02U } },
    {  8'000'000U,   250'000U, { 0x00U, 0xB1U, 0x05U } },
    {  8'000'000U,   200'000U, { 0x00U, 0xB4U, 0x06U } },
    {  8'000'000U,   125'000U, { 0x01U, 0xB1U, 0x05U } },
    {  8'000'000U,   100'000U, { 0x01U, 0xB4U, 0x06U } },
    {  8'000'000U,    80'000U, { 0x01U, 0xBFU, 0x07U } },
    {  8'000'000U,    50'000U, { 0x03U, 0xB4U, 0x06U } },
    {  8'000'000U,    40'000U, { 0x03U, 0xBFU, 0x07U } },
    {  8'000'000U,    20'000U, { 0x07U, 0xBFU, 0x07U } },
    {  8'000'000U,    10'000U, { 0x0FU, 0xBFU, 0x07U } },
    {  8'000'000U,     5'000U, { 0x1FU, 0xBFU, 0x07U } },

    { 16'000'000U, 1'000'000U, { 0x00U, 0xD0U, 0x82U } },
    { 16'000'000U,   500'000U, { 0x00U, 0xF0U, 0x86U } },
    { 16'000'000U,   250'000U, { 0x41U, 0xF1U, 0x85U } },
    { 16'000'000U,   200'000U, { 0x01U, 0xFAU, 0x87U } },
    { 16'000'000U,   125'000U, { 0x03U, 0xF0U, 0x86U } },
    { 16'000'000U,   100'000U, { 0x03U, 0xFAU, 0x87U } },
    { 16'000'000U,    80'000U, { 0x03U, 0xFFU, 0x87U } },
    { 16'000'000U,    50'000U, { 0x07U, 0xFAU, 0x87U } },
    { 16'000'000U,    40'000U, { 0x07U, 0xFFU, 0x87U } },
    { 16'000'000U,    20'000U, { 0x0FU, 0xFFU, 0x87U } },
    { 16'000'000U,    10'000U, { 0x1FU, 0xFFU, 0x87U } },
    { 16'000'000U,     5'000U, { 0x3FU, 0xFFU, 0x87U } },
    // clang-format on
  };

  const uint8_t* cnf = nullptr;

  for(const CnfEntry& entry : cnfMapper) {
    // cppcheck-suppress useStlAlgorithm
    if(entry.clockFrequency == clockFrequency && entry.baudRate == baudRate) {
      cnf = entry.cnf;
      break;
    }
  }

  if(cnf == nullptr) { return 0U; }

  writeRegister(regCnf1, cnf[0]);
  writeRegister(regCnf2, cnf[1]);
  writeRegister(regCnf3, cnf[2]);

  writeRegister(regCanInte, static_cast<uint8_t>(flagRxnIe(1U) | flagRxnIe(0U)));
  writeRegister(regBfpCtrl, 0x00U);
  writeRegister(regTxRtsCtrl, 0x00U);
  writeRegister(regRxBnCtrl(0U), static_cast<uint8_t>(flagRxm1 | flagRxm0));
  writeRegister(regRxBnCtrl(1U), static_cast<uint8_t>(flagRxm1 | flagRxm0));

  writeRegister(regCanCtrl, 0x00U);
  if(readRegister(regCanCtrl) != 0x00U) { return 0U; }

  return 1U;
}

void MCP2515::end() {
  SPI.end();
  CANController::end();
}

uint8_t MCP2515::endPacket() {
  if(!CANController::endPacket()) { return 0U; }

  const uint8_t n = 0U;

  // Build frame header + data into a contiguous buffer and send in one SPI burst.
  // TX buffer registers are consecutive: SIDH, SIDL, EID8, EID0, DLC, D0..D7
  uint8_t frame[13];
  const uint32_t id = txId;
  if(txExtended) {
    frame[0] = static_cast<uint8_t>(id >> 21);
    frame[1] = static_cast<uint8_t>(((id >> 18 & 0x07U) << 5) | flagExide | (id >> 16 & 0x03U));
    frame[2] = static_cast<uint8_t>(id >> 8);
    frame[3] = static_cast<uint8_t>(id);
  } else {
    frame[0] = static_cast<uint8_t>(id >> 3);
    frame[1] = static_cast<uint8_t>(id << 5);
    frame[2] = 0x00U;
    frame[3] = 0x00U;
  }

  uint8_t frameLen = 5U;
  if(txRtr) {
    frame[4] = static_cast<uint8_t>(0x40U | txLength);
  } else {
    frame[4] = txLength;
    memcpy(&frame[5], txData, txLength);
    frameLen = static_cast<uint8_t>(5U + txLength);
  }

  writeBurst(regTxBnSidh(n), frame, frameLen);

  writeRegister(regTxBnCtrl(n), 0x08U);

  bool aborted = false;

  uint8_t ctrl = readRegister(regTxBnCtrl(n));
  while((ctrl & 0x08U) != 0U) {
    if((ctrl & 0x10U) != 0U) {
      aborted = true;
      modifyRegister(regCanCtrl, 0x10U, 0x10U);
    }
    yield();
    ctrl = readRegister(regTxBnCtrl(n));
  }

  if(aborted) {
    modifyRegister(regCanCtrl, 0x10U, 0x00U);
  }

  modifyRegister(regCanIntf, flagTxnIf(n), 0x00U);

  // Use the cached ctrl value from the loop — avoids a redundant SPI read.
  return ((ctrl & 0x70U) != 0U) ? 0U : 1U;
}

uint8_t MCP2515::parsePacket() {
  const uint8_t intf = readRegister(regCanIntf);

  uint8_t n = 0U;
  if((intf & flagRxnIf(0U)) != 0U) {
    n = 0U;
  } else if((intf & flagRxnIf(1U)) != 0U) {
    n = 1U;
  } else {
    rxId = noId;
    rxExtended = false;
    rxRtr = false;
    rxLength = 0U;
    return 0U;
  }

  // Read SIDH, SIDL, EID8, EID0, DLC in one burst (registers are consecutive).
  uint8_t header[5];
  readBurst(regRxBnSidh(n), header, 5U);

  const uint8_t sidh = header[0];
  const uint8_t sidl = header[1];
  const uint8_t eid8 = header[2];
  const uint8_t eid0 = header[3];
  const uint8_t dlc = header[4];

  rxExtended = (sidl & flagIde) != 0U;

  const uint32_t idA = static_cast<uint32_t>(((sidh << 3) & 0x07F8U) | ((sidl >> 5) & 0x07U));
  if(rxExtended) {
    const uint32_t idB =
        (static_cast<uint32_t>(sidl & 0x03U) << 16U) |
        (static_cast<uint32_t>(eid8) << 8U) |
        static_cast<uint32_t>(eid0);

    rxId = (idA << 18U) | idB;
    rxRtr = (dlc & flagRtr) != 0U;
  } else {
    rxId = idA;
    rxRtr = (sidl & flagSrr) != 0U;
  }
  rxDlc = dlc & 0x0FU;
  rxIndex = 0U;

  if(rxRtr) {
    rxLength = 0U;
  } else {
    rxLength = rxDlc;
    if(rxLength > 0U) {
      readBurst(regRxBnD0(n), rxData, rxLength);
    }
  }

  modifyRegister(regCanIntf, flagRxnIf(n), 0x00U);

  return rxDlc;
}

void MCP2515::onReceive(void (*callback)(int)) {
  CANController::onReceive(callback);

  pinMode(intPin, INPUT);

  if(callback != nullptr) {
    SPI.usingInterrupt(digitalPinToInterrupt(intPin));
    attachInterrupt(digitalPinToInterrupt(intPin), MCP2515::onInterrupt, LOW);
  } else {
    detachInterrupt(digitalPinToInterrupt(intPin));
#if defined(SPI_HAS_NOTUSINGINTERRUPT)
    SPI.notUsingInterrupt(digitalPinToInterrupt(intPin));
#endif
  }
}

uint8_t MCP2515::filter(uint16_t id, uint16_t mask) {
  id &= 0x7FFU;
  mask &= 0x7FFU;

  writeRegister(regCanCtrl, 0x80U);
  if(readRegister(regCanCtrl) != 0x80U) { return 0U; }

  for(uint8_t n = 0U; n < 2U; n++) {
    writeRegister(regRxBnCtrl(n), flagRxm0);

    writeRegister(regRxMnSidh(n), static_cast<uint8_t>(mask >> 3));
    writeRegister(regRxMnSidl(n), static_cast<uint8_t>(mask << 5));
    writeRegister(regRxMnEid8(n), 0x00U);
    writeRegister(regRxMnEid0(n), 0x00U);
  }

  for(uint8_t n = 0U; n < 6U; n++) {
    writeRegister(regRxFnSidh(n), static_cast<uint8_t>(id >> 3));
    writeRegister(regRxFnSidl(n), static_cast<uint8_t>(id << 5));
    writeRegister(regRxFnEid8(n), 0x00U);
    writeRegister(regRxFnEid0(n), 0x00U);
  }

  writeRegister(regCanCtrl, 0x00U);
  if(readRegister(regCanCtrl) != 0x00U) { return 0U; }

  return 1U;
}

uint8_t MCP2515::filterExtended(uint32_t id, uint32_t mask) {
  id &= 0x1FFFFFFFU;
  mask &= 0x1FFFFFFFU;

  writeRegister(regCanCtrl, 0x80U);
  if(readRegister(regCanCtrl) != 0x80U) { return 0U; }

  for(uint8_t n = 0U; n < 2U; n++) {
    writeRegister(regRxBnCtrl(n), flagRxm1);

    writeRegister(regRxMnSidh(n), static_cast<uint8_t>(mask >> 21));
    writeRegister(regRxMnSidl(n), static_cast<uint8_t>((((mask >> 18) & 0x03U) << 5) | flagExide | ((mask >> 16) & 0x03U)));
    writeRegister(regRxMnEid8(n), static_cast<uint8_t>((mask >> 8) & 0xFFU));
    writeRegister(regRxMnEid0(n), static_cast<uint8_t>(mask & 0xFFU));
  }

  for(uint8_t n = 0U; n < 6U; n++) {
    writeRegister(regRxFnSidh(n), static_cast<uint8_t>(id >> 21));
    writeRegister(regRxFnSidl(n), static_cast<uint8_t>((((id >> 18) & 0x03U) << 5) | flagExide | ((id >> 16) & 0x03U)));
    writeRegister(regRxFnEid8(n), static_cast<uint8_t>((id >> 8) & 0xFFU));
    writeRegister(regRxFnEid0(n), static_cast<uint8_t>(id & 0xFFU));
  }

  writeRegister(regCanCtrl, 0x00U);
  if(readRegister(regCanCtrl) != 0x00U) { return 0U; }

  return 1U;
}

uint8_t MCP2515::observe() {
  writeRegister(regCanCtrl, 0x60U);
  if(readRegister(regCanCtrl) != 0x60U) { return 0U; }
  return 1U;
}

uint8_t MCP2515::loopback() {
  writeRegister(regCanCtrl, 0x40U);
  if(readRegister(regCanCtrl) != 0x40U) { return 0U; }
  return 1U;
}

uint8_t MCP2515::sleep() {
  writeRegister(regCanCtrl, 0x01U);
  if(readRegister(regCanCtrl) != 0x01U) { return 0U; }
  return 1U;
}

uint8_t MCP2515::wakeup() {
  writeRegister(regCanCtrl, 0x00U);
  if(readRegister(regCanCtrl) != 0x00U) { return 0U; }
  return 1U;
}

void MCP2515::setPins(uint8_t cs, uint8_t irq) {
  csPin = cs;
  intPin = irq;
}

void MCP2515::setSPIFrequency(uint32_t frequency) {
  spiSettings = SPISettings(frequency, MSBFIRST, SPI_MODE0);
}

void MCP2515::setClockFrequency(uint32_t freq) {
  clockFrequency = freq;
}

void MCP2515::dumpRegisters(Stream& out) { // NOLINT(readability-convert-member-functions-to-static)
  for(uint8_t i = 0U; i < 128U; i++) {
    const uint8_t b = readRegister(i);

    out.print("0x");
    if(i < 16U) { out.print('0'); }
    out.print(i, HEX);
    out.print(": 0x");
    if(b < 16U) { out.print('0'); }
    out.println(b, HEX);
  }
}

void MCP2515::reset() const { // NOLINT(readability-convert-member-functions-to-static)
  SPI.beginTransaction(spiSettings);
  digitalWrite(csPin, LOW);
  SPI.transfer(0xC0U);
  digitalWrite(csPin, HIGH);
  SPI.endTransaction();

  delayMicroseconds(10);
}

void MCP2515::handleInterrupt() {
  if(readRegister(regCanIntf) == 0U) { return; }
  if(onReceiveCb == nullptr) { return; }

  // DLC=0 packets (RTR or zero-byte data frames) cause parsePacket() to return 0
  // even though a valid packet was received; rxId != noId handles that case.
  while(parsePacket() != 0U || rxId != noId) {
    onReceiveCb(available());
  }
}

uint8_t MCP2515::readRegister(uint8_t address) const { // NOLINT(readability-convert-member-functions-to-static)
  SPI.beginTransaction(spiSettings);
  digitalWrite(csPin, LOW);
  SPI.transfer(0x03U);
  SPI.transfer(address);
  const uint8_t value = SPI.transfer(0x00U);
  digitalWrite(csPin, HIGH);
  SPI.endTransaction();

  return value;
}

void MCP2515::readBurst(uint8_t address, uint8_t* data, uint8_t length) const { // NOLINT(readability-convert-member-functions-to-static)
  SPI.beginTransaction(spiSettings);
  digitalWrite(csPin, LOW);
  SPI.transfer(0x03U);
  SPI.transfer(address);
  for(uint8_t i = 0U; i < length; i++) {
    data[i] = SPI.transfer(0x00U);
  }
  digitalWrite(csPin, HIGH);
  SPI.endTransaction();
}

void MCP2515::modifyRegister(uint8_t address, uint8_t mask, uint8_t value) const {
  SPI.beginTransaction(spiSettings);
  digitalWrite(csPin, LOW);
  SPI.transfer(0x05U);
  SPI.transfer(address);
  SPI.transfer(mask);
  SPI.transfer(value);
  digitalWrite(csPin, HIGH);
  SPI.endTransaction();
}

void MCP2515::writeRegister(uint8_t address, uint8_t value) const {
  SPI.beginTransaction(spiSettings);
  digitalWrite(csPin, LOW);
  SPI.transfer(0x02U);
  SPI.transfer(address);
  SPI.transfer(value);
  digitalWrite(csPin, HIGH);
  SPI.endTransaction();
}

void MCP2515::writeBurst(uint8_t address, const uint8_t* data, uint8_t length) const { // NOLINT(readability-convert-member-functions-to-static)
  SPI.beginTransaction(spiSettings);
  digitalWrite(csPin, LOW);
  SPI.transfer(0x02U);
  SPI.transfer(address);
  for(uint8_t i = 0U; i < length; i++) {
    SPI.transfer(data[i]);
  }
  digitalWrite(csPin, HIGH);
  SPI.endTransaction();
}

void MCP2515::onInterrupt() {
  CAN.handleInterrupt();
}

MCP2515 CAN;

#endif // !ARDUINO_ARCH_ESP32
