#include "SPIFlash.h"
#include <string.h>                                                 /// memset() for the failure paths.

SPIFlash::SPIFlash(uint8_t slaveSelectPin, uint16_t jedecID) :
  slaveSelectPin(slaveSelectPin),
  jedecID(jedecID) {}

void SPIFlash::select() { // NOLINT(readability-convert-member-functions-to-static,readability-make-member-function-const)
#ifndef SPI_HAS_TRANSACTION
  noInterrupts();
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
}

bool SPIFlash::initialize() {
  pinMode(slaveSelectPin, OUTPUT);
  SPI.begin();
#ifdef SPI_HAS_TRANSACTION
  settings = SPISettings(4000000U, MSBFIRST, SPI_MODE0);
#endif
  unselect();
  wakeup();
  if(jedecID == 0U || readDeviceId() == jedecID) {
    if(!command(CMD_STATUS_WRITE, true)) { return false; }        // Write Status Register.
    SPI.transfer(0U);                // Global Unprotect.
    unselect();
    return true;
  }
  return false;
}

uint16_t SPIFlash::readDeviceId() {
  if(!command(CMD_READ_ID)) { return 0U; }
  const uint16_t jedecid = (static_cast<uint16_t>(SPI.transfer(0U)) << 8U) | SPI.transfer(0U);
  unselect();
  return jedecid;
}

uint32_t SPIFlash::capacity() {
  if(!command(CMD_READ_ID)) { return 0U; }
  (void)SPI.transfer(0U);                                          // Byte 1: manufacturer ID.
  (void)SPI.transfer(0U);                                          // Byte 2: memory type.
  const uint8_t densityCode = SPI.transfer(0U);                    // Byte 3: density (size = 2^code bytes).
  unselect();
  // 0x00 / 0xFF (absent chip) or a code that would overflow a 32-bit size mean "unknown".
  if(densityCode == 0U || densityCode >= 32U) { return 0U; }
  return static_cast<uint32_t>(1UL) << densityCode;
}

void SPIFlash::readUniqueId(uint8_t (&buf)[8]) {
  if(!command(CMD_READ_MAC)) {
    memset(buf, 0, sizeof(buf));
    return;
  }
  SPI.transfer(0U);
  SPI.transfer(0U);
  SPI.transfer(0U);
  SPI.transfer(0U);
  for(uint8_t& value : buf) {
    value = SPI.transfer(0U);
  }
  unselect();
}

uint8_t SPIFlash::readByte(uint32_t addr) {
  if(!command(CMD_ARRAY_READ_LF)) { return 0U; }
  SPI.transfer(static_cast<uint8_t>(addr >> 16U));
  SPI.transfer(static_cast<uint8_t>(addr >> 8U));
  SPI.transfer(static_cast<uint8_t>(addr));
  const uint8_t result = SPI.transfer(0U);
  unselect();
  return result;
}

bool SPIFlash::readBytes(uint32_t addr, void* buf, uint16_t len) {
  if(!command(CMD_ARRAY_READ)) {
    memset(buf, 0, len);
    return false;
  }
  SPI.transfer(static_cast<uint8_t>(addr >> 16U));
  SPI.transfer(static_cast<uint8_t>(addr >> 8U));
  SPI.transfer(static_cast<uint8_t>(addr));
  SPI.transfer(0U); // Dummy byte required by fast-read command.
  uint8_t* dest = static_cast<uint8_t*>(buf);
  for(uint16_t i = 0U; i < len; ++i) {
    dest[i] = SPI.transfer(0U);
  }
  unselect();
  return true;
}

bool SPIFlash::waitUntilReady() {
  const uint32_t startTime = millis();
  for(;;) {
    const uint8_t status = readStatus();
    // All ones means nothing is driving MISO: an absent or unresponsive chip. The busy bit on
    // its own reads as set in that case, indistinguishable from a write in progress.
    if(status == statusNotResponding) { return false; }
    if((status & 1U) == 0U) { return true; }
    if((millis() - startTime) > busyTimeoutMs) { return false; }
  }
}

bool SPIFlash::command(uint8_t cmd, bool isWrite) {
  if(isWrite) {
    if(!command(CMD_WRITE_ENABLE)) { return false; }
    unselect();
  }
  if((cmd != CMD_WAKE) && !waitUntilReady()) { return false; }
  select();
  SPI.transfer(cmd);
  return true;
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

bool SPIFlash::writeByte(uint32_t addr, uint8_t byt) {
  if(!command(CMD_BYTE_PROGRAM, true)) { return false; }
  SPI.transfer(static_cast<uint8_t>(addr >> 16U));
  SPI.transfer(static_cast<uint8_t>(addr >> 8U));
  SPI.transfer(static_cast<uint8_t>(addr));
  SPI.transfer(byt);
  unselect();
  return true;
}

bool SPIFlash::writeBytes(uint32_t addr, const void* buf, uint16_t len) {
  uint16_t maxBytes = static_cast<uint16_t>(256U - (addr % 256U)); // Keep the first write within the first page.
  const uint8_t* ptr = static_cast<const uint8_t*>(buf);
  while(len > 0U) {
    const uint16_t n = (len <= maxBytes) ? len : maxBytes;
    if(!command(CMD_BYTE_PROGRAM, true)) { return false; }
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
  return true;
}

bool SPIFlash::chipErase() {
  if(!command(CMD_ERASE_CHIP, true)) { return false; }
  unselect();
  return true;
}

bool SPIFlash::blockErase4K(uint32_t addr) {
  if(!command(CMD_ERASE_4K, true)) { return false; }
  SPI.transfer(static_cast<uint8_t>(addr >> 16U));
  SPI.transfer(static_cast<uint8_t>(addr >> 8U));
  SPI.transfer(static_cast<uint8_t>(addr));
  unselect();
  return true;
}

bool SPIFlash::blockErase32K(uint32_t addr) {
  if(!command(CMD_ERASE_32K, true)) { return false; }
  SPI.transfer(static_cast<uint8_t>(addr >> 16U));
  SPI.transfer(static_cast<uint8_t>(addr >> 8U));
  SPI.transfer(static_cast<uint8_t>(addr));
  unselect();
  return true;
}

bool SPIFlash::blockErase64K(uint32_t addr) {
  if(!command(CMD_ERASE_64K, true)) { return false; }
  SPI.transfer(static_cast<uint8_t>(addr >> 16U));
  SPI.transfer(static_cast<uint8_t>(addr >> 8U));
  SPI.transfer(static_cast<uint8_t>(addr));
  unselect();
  return true;
}

void SPIFlash::sleep() {
  if(!command(CMD_SLEEP)) { return; }
  unselect();
}

void SPIFlash::wakeup() {
  (void)command(CMD_WAKE);                                          // CMD_WAKE skips the ready wait, so this cannot fail.
  unselect();
}

void SPIFlash::end() { // NOLINT(readability-convert-member-functions-to-static) mirrors the instance API
  SPI.end();
}
