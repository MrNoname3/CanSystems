#if defined(ARDUINO_ARCH_ESP32)

#include "esp_intr_alloc.h"
#include "soc/dport_reg.h"
#include "driver/gpio.h"

#include "ESP32SJA1000.h"

namespace {
  constexpr uint32_t regBase = 0x3FF6B000U;

  constexpr uint8_t regMod   = 0x00U;
  constexpr uint8_t regCmr   = 0x01U;
  constexpr uint8_t regSr    = 0x02U;
  constexpr uint8_t regIr    = 0x03U;
  constexpr uint8_t regIer   = 0x04U;
  constexpr uint8_t regBtr0  = 0x06U;
  constexpr uint8_t regBtr1  = 0x07U;
  constexpr uint8_t regOcr   = 0x08U;
  constexpr uint8_t regEcc   = 0x0CU;
  constexpr uint8_t regRxErr = 0x0EU;
  constexpr uint8_t regTxErr = 0x0FU;
  constexpr uint8_t regSff   = 0x10U;
  constexpr uint8_t regEff   = 0x10U;
  constexpr uint8_t regCdr   = 0x1FU;

  constexpr uint8_t regAcrN(uint8_t n) { return static_cast<uint8_t>(0x10U + n); }
  constexpr uint8_t regAmrN(uint8_t n) { return static_cast<uint8_t>(0x14U + n); }
} // namespace

uint8_t ESP32SJA1000::begin(uint32_t baudRate) {
  if(CANController::begin(baudRate) != 1) { return 0U; }

  loopbackEnabled = false;

  DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, DPORT_CAN_RST);
  DPORT_SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_CAN_CLK_EN);

  // RX pin
  gpio_set_direction(rxPin, GPIO_MODE_INPUT);
  gpio_matrix_in(rxPin, CAN_RX_IDX, 0);
  gpio_pad_select_gpio(rxPin);

  // TX pin
  gpio_set_direction(txPin, GPIO_MODE_OUTPUT);
  gpio_matrix_out(txPin, CAN_TX_IDX, 0, 0);
  gpio_pad_select_gpio(txPin);

  modifyRegister(regCdr,  0x80U, 0x80U); // pelican mode
  modifyRegister(regBtr0, 0xC0U, 0x40U); // SJW = 1
  modifyRegister(regBtr1, 0x70U, 0x10U); // TSEG2 = 1

  switch(baudRate) {
    case 1'000'000U:
      modifyRegister(regBtr1, 0x0FU, 0x04U);
      modifyRegister(regBtr0, 0x3FU, 4U);
      break;

    case 500'000U:
      modifyRegister(regBtr1, 0x0FU, 0x0CU);
      modifyRegister(regBtr0, 0x3FU, 4U);
      break;

    case 250'000U:
      modifyRegister(regBtr1, 0x0FU, 0x0CU);
      modifyRegister(regBtr0, 0x3FU, 9U);
      break;

    case 200'000U:
      modifyRegister(regBtr1, 0x0FU, 0x0CU);
      modifyRegister(regBtr0, 0x3FU, 12U);
      break;

    case 125'000U:
      modifyRegister(regBtr1, 0x0FU, 0x0CU);
      modifyRegister(regBtr0, 0x3FU, 19U);
      break;

    case 100'000U:
      modifyRegister(regBtr1, 0x0FU, 0x0CU);
      modifyRegister(regBtr0, 0x3FU, 24U);
      break;

    case 80'000U:
      modifyRegister(regBtr1, 0x0FU, 0x0CU);
      modifyRegister(regBtr0, 0x3FU, 30U);
      break;

    case 50'000U:
      modifyRegister(regBtr1, 0x0FU, 0x0CU);
      modifyRegister(regBtr0, 0x3FU, 49U);
      break;

    /*
     * Due to limitations in ESP32 hardware and/or RTOS software, baudrate
     * can't be lower than 50kbps.
     * See https://esp32.com/viewtopic.php?t=2142
     */
    default:
      return 0U;
  }

  modifyRegister(regBtr1, 0x80U, 0x80U); // SAM = 1
  writeRegister(regIer, 0xFFU);          // enable all interrupts

  // set filter to allow anything
  writeRegister(regAcrN(0U), 0x00U);
  writeRegister(regAcrN(1U), 0x00U);
  writeRegister(regAcrN(2U), 0x00U);
  writeRegister(regAcrN(3U), 0x00U);
  writeRegister(regAmrN(0U), 0xFFU);
  writeRegister(regAmrN(1U), 0xFFU);
  writeRegister(regAmrN(2U), 0xFFU);
  writeRegister(regAmrN(3U), 0xFFU);

  modifyRegister(regOcr, 0x03U, 0x02U); // normal output mode
  // reset error counters
  writeRegister(regTxErr, 0x00U);
  writeRegister(regRxErr, 0x00U);

  // clear errors and interrupts
  readRegister(regEcc);
  readRegister(regIr);

  // normal mode
  modifyRegister(regMod, 0x08U, 0x08U);
  modifyRegister(regMod, 0x17U, 0x00U);

  return 1U;
}

void ESP32SJA1000::end() {
  if(intrHandle != nullptr) {
    esp_intr_free(intrHandle);
    intrHandle = nullptr;
  }

  DPORT_SET_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, DPORT_CAN_RST);
  DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_CAN_CLK_EN);

  CANController::end();
}

uint8_t ESP32SJA1000::endPacket() {
  if(!CANController::endPacket()) { return 0U; }

  // wait for TX buffer to free
  while((readRegister(regSr) & 0x04U) != 0x04U) {
    yield();
  }

  uint8_t dataReg;

  if(txExtended) {
    writeRegister(regEff, static_cast<uint8_t>(0x80U | (txRtr ? 0x40U : 0x00U) | (0x0FU & static_cast<uint8_t>(txLength))));
    writeRegister(static_cast<uint8_t>(regEff + 1U), static_cast<uint8_t>(txId >> 21));
    writeRegister(static_cast<uint8_t>(regEff + 2U), static_cast<uint8_t>(txId >> 13));
    writeRegister(static_cast<uint8_t>(regEff + 3U), static_cast<uint8_t>(txId >> 5));
    writeRegister(static_cast<uint8_t>(regEff + 4U), static_cast<uint8_t>(txId << 3));

    dataReg = static_cast<uint8_t>(regEff + 5U);
  } else {
    writeRegister(regSff, static_cast<uint8_t>((txRtr ? 0x40U : 0x00U) | (0x0FU & static_cast<uint8_t>(txLength))));
    writeRegister(static_cast<uint8_t>(regSff + 1U), static_cast<uint8_t>(txId >> 3));
    writeRegister(static_cast<uint8_t>(regSff + 2U), static_cast<uint8_t>(txId << 5));

    dataReg = static_cast<uint8_t>(regSff + 3U);
  }

  for(uint8_t i = 0U; i < txLength; i++) {
    writeRegister(static_cast<uint8_t>(dataReg + i), txData[i]);
  }

  if(loopbackEnabled) {
    modifyRegister(regCmr, 0x1FU, 0x10U); // self reception request
  } else {
    modifyRegister(regCmr, 0x1FU, 0x01U); // transmit request
  }

  // wait for TX complete
  while((readRegister(regSr) & 0x08U) != 0x08U) {
    if(readRegister(regEcc) == 0xD9U) {
      modifyRegister(regCmr, 0x1FU, 0x02U); // error, abort
      return 0U;
    }
    yield();
  }

  return 1U;
}

uint8_t ESP32SJA1000::parsePacket() {
  if((readRegister(regSr) & 0x01U) != 0x01U) { return 0U; }

  rxExtended = (readRegister(regSff) & 0x80U) != 0U;
  rxRtr      = (readRegister(regSff) & 0x40U) != 0U;
  rxDlc      = readRegister(regSff) & 0x0FU;
  rxIndex    = 0U;

  uint8_t dataReg;

  if(rxExtended) {
    rxId = static_cast<int32_t>(
      (static_cast<uint32_t>(readRegister(static_cast<uint8_t>(regEff + 1U))) << 21U) |
      (static_cast<uint32_t>(readRegister(static_cast<uint8_t>(regEff + 2U))) << 13U) |
      (static_cast<uint32_t>(readRegister(static_cast<uint8_t>(regEff + 3U))) << 5U)  |
      (static_cast<uint32_t>(readRegister(static_cast<uint8_t>(regEff + 4U))) >> 3U));

    dataReg = static_cast<uint8_t>(regEff + 5U);
  } else {
    rxId = static_cast<int32_t>(
      (static_cast<uint32_t>(readRegister(static_cast<uint8_t>(regSff + 1U))) << 3U) |
      ((static_cast<uint32_t>(readRegister(static_cast<uint8_t>(regSff + 2U))) >> 5U) & 0x07U));

    dataReg = static_cast<uint8_t>(regSff + 3U);
  }

  if(rxRtr) {
    rxLength = 0U;
  } else {
    rxLength = rxDlc;

    for(uint8_t i = 0U; i < rxLength; i++) {
      rxData[i] = readRegister(static_cast<uint8_t>(dataReg + i));
    }
  }

  modifyRegister(regCmr, 0x04U, 0x04U); // release RX buffer

  return rxDlc;
}

void ESP32SJA1000::onReceive(void(*callback)(int)) {
  CANController::onReceive(callback);

  if(intrHandle != nullptr) {
    esp_intr_free(intrHandle);
    intrHandle = nullptr;
  }

  if(callback != nullptr) {
    esp_intr_alloc(ETS_CAN_INTR_SOURCE, 0, ESP32SJA1000::onInterrupt, this, &intrHandle);
  }
}

uint8_t ESP32SJA1000::filter(uint16_t id, uint16_t mask) {
  id &= 0x7FFU;
  const uint16_t amr = static_cast<uint16_t>(~(mask & 0x7FFU));

  modifyRegister(regMod, 0x17U, 0x01U); // reset

  writeRegister(regAcrN(0U), static_cast<uint8_t>(id >> 3));
  writeRegister(regAcrN(1U), static_cast<uint8_t>(id << 5));
  writeRegister(regAcrN(2U), 0x00U);
  writeRegister(regAcrN(3U), 0x00U);

  writeRegister(regAmrN(0U), static_cast<uint8_t>(amr >> 3));
  writeRegister(regAmrN(1U), static_cast<uint8_t>((amr << 5) | 0x1FU));
  writeRegister(regAmrN(2U), 0xFFU);
  writeRegister(regAmrN(3U), 0xFFU);

  modifyRegister(regMod, 0x17U, 0x00U); // normal

  return 1U;
}

uint8_t ESP32SJA1000::filterExtended(uint32_t id, uint32_t mask) {
  id &= 0x1FFFFFFFU;
  const uint32_t amr = ~(mask & 0x1FFFFFFFU);

  modifyRegister(regMod, 0x17U, 0x01U); // reset

  writeRegister(regAcrN(0U), static_cast<uint8_t>(id >> 21));
  writeRegister(regAcrN(1U), static_cast<uint8_t>(id >> 13));
  writeRegister(regAcrN(2U), static_cast<uint8_t>(id >> 5));
  writeRegister(regAcrN(3U), static_cast<uint8_t>(id << 3));

  writeRegister(regAmrN(0U), static_cast<uint8_t>(amr >> 21));
  writeRegister(regAmrN(1U), static_cast<uint8_t>(amr >> 13));
  writeRegister(regAmrN(2U), static_cast<uint8_t>(amr >> 5));
  writeRegister(regAmrN(3U), static_cast<uint8_t>((amr << 3) | 0x7FU));

  modifyRegister(regMod, 0x17U, 0x00U); // normal

  return 1U;
}

uint8_t ESP32SJA1000::observe() {
  modifyRegister(regMod, 0x17U, 0x01U); // reset
  modifyRegister(regMod, 0x17U, 0x02U); // observe
  return 1U;
}

uint8_t ESP32SJA1000::loopback() {
  loopbackEnabled = true;

  modifyRegister(regMod, 0x17U, 0x01U); // reset
  modifyRegister(regMod, 0x17U, 0x04U); // self test mode

  return 1U;
}

uint8_t ESP32SJA1000::sleep() {
  modifyRegister(regMod, 0x1FU, 0x10U);
  return 1U;
}

uint8_t ESP32SJA1000::wakeup() {
  modifyRegister(regMod, 0x1FU, 0x00U);
  return 1U;
}

void ESP32SJA1000::setPins(int rx, int tx) {
  rxPin = static_cast<gpio_num_t>(rx);
  txPin = static_cast<gpio_num_t>(tx);
}

void ESP32SJA1000::dumpRegisters(Stream& out) { // NOLINT(readability-convert-member-functions-to-static)
  for(uint8_t i = 0U; i < 32U; i++) {
    const uint8_t b = readRegister(i);

    out.print("0x");
    if(i < 16U) { out.print('0'); }
    out.print(i, HEX);
    out.print(": 0x");
    if(b < 16U) { out.print('0'); }
    out.println(b, HEX);
  }
}

void ESP32SJA1000::handleInterrupt() {
  const uint8_t ir = readRegister(regIr);

  if(ir & 0x01U) {
    if(parsePacket() == 0) { return; }
    onReceiveCb(available());
  }
}

uint8_t ESP32SJA1000::readRegister(uint8_t address) {
  volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(regBase + static_cast<uint32_t>(address) * 4U);
  return static_cast<uint8_t>(*reg);
}

void ESP32SJA1000::modifyRegister(uint8_t address, uint8_t mask, uint8_t value) { // NOLINT(readability-convert-member-functions-to-static)
  volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(regBase + static_cast<uint32_t>(address) * 4U);
  *reg = (*reg & ~static_cast<uint32_t>(mask)) | value;
}

void ESP32SJA1000::writeRegister(uint8_t address, uint8_t value) { // NOLINT(readability-convert-member-functions-to-static)
  volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(regBase + static_cast<uint32_t>(address) * 4U);
  *reg = value;
}

void ESP32SJA1000::onInterrupt(void* arg) {
  static_cast<ESP32SJA1000*>(arg)->handleInterrupt();
}

ESP32SJA1000 CAN;

#endif // ARDUINO_ARCH_ESP32
