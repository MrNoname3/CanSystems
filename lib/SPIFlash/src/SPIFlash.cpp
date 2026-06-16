#include "SPIFlash.h"

SPIFlash::SPIFlash(uint8_t slaveSelectPin, uint16_t jedecID) :
  slaveSelectPin(slaveSelectPin),
  jedecID(jedecID),
  spcr(0U),
  spsr(0U) {}

void SPIFlash::select() { // NOLINT(readability-convert-member-functions-to-static,readability-make-member-function-const)
  // Save current SPI settings.
#ifndef SPI_HAS_TRANSACTION
  noInterrupts();
#endif
#if defined(SPCR) && defined(SPSR)
  spcr = SPCR;
  spsr = SPSR;
#endif
#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(settings);
#else
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  SPI.setClockDivider(SPI_CLOCK_DIV4); // Slowed from DIV2 to avoid SPI stalls (especially with RFM69 + FLASH on mega1284p).
#endif
  digitalWrite(slaveSelectPin, LOW);
}

void SPIFlash::unselect() { // NOLINT(readability-convert-member-functions-to-static,readability-make-member-function-const)
  digitalWrite(slaveSelectPin, HIGH);
  // Restore SPI settings to what they were before talking to the FLASH chip.
#ifdef SPI_HAS_TRANSACTION
  SPI.endTransaction();
#else
  interrupts();
#endif
#if defined(SPCR) && defined(SPSR)
  SPCR = spcr;
  SPSR = spsr;
#endif
}

bool SPIFlash::initialize() {
#if defined(SPCR) && defined(SPSR)
  spcr = SPCR;
  spsr = SPSR;
#endif
  pinMode(slaveSelectPin, OUTPUT);
  SPI.begin();
#ifdef SPI_HAS_TRANSACTION
  settings = SPISettings(4000000U, MSBFIRST, SPI_MODE0);
#endif
  unselect();
  wakeup();
  if(jedecID == 0U || readDeviceId() == jedecID) {
    command(CMD_STATUS_WRITE, true); // Write Status Register.
    SPI.transfer(0U);                // Global Unprotect.
    unselect();
    return true;
  }
  return false;
}

uint16_t SPIFlash::readDeviceId() {
#if defined(__AVR_ATmega32U4__)
  command(CMD_READ_ID);
#else
  select();
  SPI.transfer(CMD_READ_ID);
#endif
  const uint16_t jedecid = (static_cast<uint16_t>(SPI.transfer(0U)) << 8U) | SPI.transfer(0U);
  unselect();
  return jedecid;
}

uint32_t SPIFlash::capacity() {
#if defined(__AVR_ATmega32U4__)
  command(CMD_READ_ID);
#else
  select();
  SPI.transfer(CMD_READ_ID);
#endif
  (void)SPI.transfer(0U);                                          // Byte 1: manufacturer ID.
  (void)SPI.transfer(0U);                                          // Byte 2: memory type.
  const uint8_t densityCode = SPI.transfer(0U);                    // Byte 3: density (size = 2^code bytes).
  unselect();
  // 0x00 / 0xFF (absent chip) or a code that would overflow a 32-bit size mean "unknown".
  if(densityCode == 0U || densityCode >= 32U) { return 0U; }
  return static_cast<uint32_t>(1UL) << densityCode;
}

void SPIFlash::readUniqueId(uint8_t (&buf)[8]) {
  command(CMD_READ_MAC);
  SPI.transfer(0U);
  SPI.transfer(0U);
  SPI.transfer(0U);
  SPI.transfer(0U);
  for(uint8_t i = 0U; i < 8U; i++) {
    buf[i] = SPI.transfer(0U);
  }
  unselect();
}

uint8_t SPIFlash::readByte(uint32_t addr) {
  command(CMD_ARRAY_READ_LF);
  SPI.transfer(static_cast<uint8_t>(addr >> 16U));
  SPI.transfer(static_cast<uint8_t>(addr >> 8U));
  SPI.transfer(static_cast<uint8_t>(addr));
  const uint8_t result = SPI.transfer(0U);
  unselect();
  return result;
}

void SPIFlash::readBytes(uint32_t addr, void* buf, uint16_t len) {
  command(CMD_ARRAY_READ);
  SPI.transfer(static_cast<uint8_t>(addr >> 16U));
  SPI.transfer(static_cast<uint8_t>(addr >> 8U));
  SPI.transfer(static_cast<uint8_t>(addr));
  SPI.transfer(0U); // Dummy byte required by fast-read command.
  uint8_t* dest = static_cast<uint8_t*>(buf);
  for(uint16_t i = 0U; i < len; ++i) {
    dest[i] = SPI.transfer(0U);
  }
  unselect();
}

void SPIFlash::command(uint8_t cmd, bool isWrite) {
#if defined(__AVR_ATmega32U4__)
  DDRB |= 0x01U;
  PORTB |= 0x01U;
#endif
  if(isWrite) {
    command(CMD_WRITE_ENABLE);
    unselect();
  }
  // Wait for any write/erase to complete. A timeout cannot be added here because chip-erase
  // can take several seconds. If the chip is absent, a weak pull-down on MISO is recommended
  // to avoid hanging.
  if(cmd != CMD_WAKE) {
    while(busy()) {}
  }
  select();
  SPI.transfer(cmd);
}

bool SPIFlash::busy() {
  return (readStatus() & 1U) != 0U;
}

uint8_t SPIFlash::readStatus() {
  select();
  SPI.transfer(CMD_STATUS_READ);
  const uint8_t status = SPI.transfer(0U);
  unselect();
  return status;
}

void SPIFlash::writeByte(uint32_t addr, uint8_t byt) {
  command(CMD_BYTE_PROGRAM, true);
  SPI.transfer(static_cast<uint8_t>(addr >> 16U));
  SPI.transfer(static_cast<uint8_t>(addr >> 8U));
  SPI.transfer(static_cast<uint8_t>(addr));
  SPI.transfer(byt);
  unselect();
}

void SPIFlash::writeBytes(uint32_t addr, const void* buf, uint16_t len) {
  uint16_t maxBytes = static_cast<uint16_t>(256U - (addr % 256U)); // Keep the first write within the first page.
  const uint8_t* ptr = static_cast<const uint8_t*>(buf);
  while(len > 0U) {
    const uint16_t n = (len <= maxBytes) ? len : maxBytes;
    command(CMD_BYTE_PROGRAM, true);
    SPI.transfer(static_cast<uint8_t>(addr >> 16U));
    SPI.transfer(static_cast<uint8_t>(addr >> 8U));
    SPI.transfer(static_cast<uint8_t>(addr));
    for(uint16_t i = 0U; i < n; i++) {
      SPI.transfer(ptr[i]);
    }
    unselect();
    addr += n;  // Advance address and pointer by the number of bytes just written.
    ptr += n;
    len -= n;
    maxBytes = 256U; // Subsequent iterations can use a full page.
  }
}

void SPIFlash::chipErase() {
  command(CMD_ERASE_CHIP, true);
  unselect();
}

void SPIFlash::blockErase4K(uint32_t addr) {
  command(CMD_ERASE_4K, true);
  SPI.transfer(static_cast<uint8_t>(addr >> 16U));
  SPI.transfer(static_cast<uint8_t>(addr >> 8U));
  SPI.transfer(static_cast<uint8_t>(addr));
  unselect();
}

void SPIFlash::blockErase32K(uint32_t addr) {
  command(CMD_ERASE_32K, true);
  SPI.transfer(static_cast<uint8_t>(addr >> 16U));
  SPI.transfer(static_cast<uint8_t>(addr >> 8U));
  SPI.transfer(static_cast<uint8_t>(addr));
  unselect();
}

void SPIFlash::blockErase64K(uint32_t addr) {
  command(CMD_ERASE_64K, true);
  SPI.transfer(static_cast<uint8_t>(addr >> 16U));
  SPI.transfer(static_cast<uint8_t>(addr >> 8U));
  SPI.transfer(static_cast<uint8_t>(addr));
  unselect();
}

void SPIFlash::sleep() {
  command(CMD_SLEEP);
  unselect();
}

void SPIFlash::wakeup() {
  command(CMD_WAKE);
  unselect();
}

void SPIFlash::end() {
  SPI.end();
}
