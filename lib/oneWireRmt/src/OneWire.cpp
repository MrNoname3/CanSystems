#include "OneWire.h"
#include "driver/gpio.h"                                            /// GPIO direction / pull configuration.
#include "esp_rom_gpio.h"                                           /// esp_rom_gpio_connect_*_signal (ROM GPIO matrix, IDF 4.4+).
#include "soc/gpio_sig_map.h"                                       /// RMT_SIG_OUT0_IDX / RMT_SIG_IN0_IDX signal indices.
#include "freertos/FreeRTOS.h"                                      /// FreeRTOS base.
#include "freertos/ringbuf.h"                                       /// RMT RX ring buffer.

namespace {
// RMT channels. The ESP32-CAM camera uses I2S/LCD, not RMT, so 0/1 are free.
// NOTE: a single shared pair of channels — only one OneWire bus instance is supported for now.
constexpr rmt_channel_t txChannel = RMT_CHANNEL_0;
constexpr rmt_channel_t rxChannel = RMT_CHANNEL_1;

// RMT clock: APB 80 MHz / 80 => 1 MHz => 1 tick = 1 microsecond.
constexpr uint8_t rmtClkDiv = 80U;

// 1-Wire standard-speed timings (microseconds). VALIDATE ON HARDWARE (scope) before trusting reads.
constexpr uint16_t resetLowUs       = 480U;   // Master reset low pulse.
constexpr uint16_t resetReleaseUs   = 480U;   // Window held high to observe the presence pulse.
constexpr uint16_t write1LowUs      = 6U;     // Write-1: short low.
constexpr uint16_t write1HighUs     = 64U;    // ... then high to fill the 70us slot.
constexpr uint16_t write0LowUs      = 60U;    // Write-0: long low.
constexpr uint16_t write0HighUs     = 10U;    // ... then high to fill the 70us slot.
constexpr uint16_t readLowUs        = 6U;     // Read: master pulls low briefly to start the slot.
constexpr uint16_t readHighUs       = 64U;    // ... then releases; RX samples the bus during this window.
constexpr uint16_t readThreshUs     = 15U;    // Sampled low <= 15us => bit 1; longer => slave held low => bit 0.
constexpr uint16_t presenceMinUs    = 40U;    // Presence pulse min length.
constexpr uint16_t presenceMaxUs    = 300U;   // Presence pulse max length.
constexpr uint16_t rxIdleThreshUs   = 1000U;  // RX considers the frame finished after this idle time.
constexpr uint8_t  rxFilterTicks    = 100U;   // Source-clock ticks (~1.25us @80MHz) glitch filter.
constexpr uint32_t rxTimeoutMs      = 5U;     // Max wait for RX items.

// Builds an rmt_item32_t from two (level,duration) halves.
inline rmt_item32_t makeItem(uint8_t level0, uint16_t dur0, uint8_t level1, uint16_t dur1) {
  rmt_item32_t item{};
  item.level0 = level0;
  item.duration0 = dur0;
  item.level1 = level1;
  item.duration1 = dur1;
  return item;
}
}  // namespace

void OneWire::begin(uint8_t pin) {
  busPin = static_cast<gpio_num_t>(pin);

  // --- TX channel: drives the bus, idles high (released). ---
  rmt_config_t txCfg{};
  txCfg.rmt_mode = RMT_MODE_TX;
  txCfg.channel = txChannel;
  txCfg.gpio_num = busPin;
  txCfg.clk_div = rmtClkDiv;
  txCfg.mem_block_num = 1U;
  txCfg.tx_config.loop_en = false;
  txCfg.tx_config.carrier_en = false;
  txCfg.tx_config.idle_output_en = true;
  txCfg.tx_config.idle_level = RMT_IDLE_LEVEL_HIGH;
  if(rmt_config(&txCfg) != ESP_OK || rmt_driver_install(txChannel, 0U, 0) != ESP_OK) { return; }

  // --- RX channel: samples the bus on the same pin. ---
  rmt_config_t rxCfg{};
  rxCfg.rmt_mode = RMT_MODE_RX;
  rxCfg.channel = rxChannel;
  rxCfg.gpio_num = busPin;
  rxCfg.clk_div = rmtClkDiv;
  rxCfg.mem_block_num = 1U;
  rxCfg.rx_config.filter_en = true;
  rxCfg.rx_config.filter_ticks_thresh = rxFilterTicks;
  rxCfg.rx_config.idle_threshold = rxIdleThreshUs;
  if(rmt_config(&rxCfg) != ESP_OK || rmt_driver_install(rxChannel, 512U, 0) != ESP_OK) { return; }

  // --- Share one open-drain pin for both channels. ---
  // rmt_config() above wired each channel's signal to the pad, but the second call overrode the
  // first. Re-route both signals onto an open-drain pad so the slave can pull the line low while
  // the TX channel idles high (the external 4.7k resistor provides the high level).
  gpio_set_pull_mode(busPin, GPIO_PULLUP_ONLY);
  gpio_set_direction(busPin, GPIO_MODE_INPUT_OUTPUT_OD);
  esp_rom_gpio_connect_out_signal(busPin, RMT_SIG_OUT0_IDX + txChannel, false, false);
  esp_rom_gpio_connect_in_signal(busPin, RMT_SIG_IN0_IDX + rxChannel, false);

  initialized = true;
}

OneWire::~OneWire() {
  if(initialized) {
    rmt_driver_uninstall(txChannel);
    rmt_driver_uninstall(rxChannel);
  }
}

void OneWire::txItem(const rmt_item32_t& item) {
  if(!initialized) { return; }
  (void)rmt_write_items(txChannel, &item, 1, true);  // true => block until the item has been sent.
  (void)rmt_wait_tx_done(txChannel, pdMS_TO_TICKS(rxTimeoutMs));
}

void OneWire::write_bit(uint8_t v) {
  const rmt_item32_t item = v ? makeItem(0U, write1LowUs, 1U, write1HighUs)
                              : makeItem(0U, write0LowUs, 1U, write0HighUs);
  txItem(item);
}

uint8_t OneWire::readSlot() {
  if(!initialized) { return 1U; }
  RingbufHandle_t rb = nullptr;
  if(rmt_get_ringbuf_handle(rxChannel, &rb) != ESP_OK || rb == nullptr) { return 1U; }

  (void)rmt_rx_start(rxChannel, true);                 // true => reset the RX memory/pointer.
  const rmt_item32_t stimulus = makeItem(0U, readLowUs, 1U, readHighUs);
  (void)rmt_write_items(txChannel, &stimulus, 1, true);
  (void)rmt_wait_tx_done(txChannel, pdMS_TO_TICKS(rxTimeoutMs));

  uint8_t bit = 1U;                                    // Default: line stayed high => logical 1.
  size_t rxSize = 0U;
  void* raw = xRingbufferReceive(rb, &rxSize, pdMS_TO_TICKS(rxTimeoutMs));
  if(raw != nullptr) {
    const rmt_item32_t* items = static_cast<rmt_item32_t*>(raw);
    if(rxSize >= sizeof(rmt_item32_t)) {
      // The slot starts with the master's low pulse; if the slave drives a 0 it holds the line low
      // well past the master's ~6us, so a long initial low decodes as bit 0.
      const uint16_t lowDur = (items[0].level0 == 0U) ? items[0].duration0 : items[0].duration1;
      bit = (lowDur <= readThreshUs) ? 1U : 0U;
    }
    vRingbufferReturnItem(rb, raw);
  }
  (void)rmt_rx_stop(rxChannel);
  return bit;
}

uint8_t OneWire::read_bit(void) {
  return readSlot();
}

uint8_t OneWire::reset(void) {
  if(!initialized) { return 0U; }
  RingbufHandle_t rb = nullptr;
  if(rmt_get_ringbuf_handle(rxChannel, &rb) != ESP_OK || rb == nullptr) { return 0U; }

  (void)rmt_rx_start(rxChannel, true);
  const rmt_item32_t resetPulse = makeItem(0U, resetLowUs, 1U, resetReleaseUs);
  (void)rmt_write_items(txChannel, &resetPulse, 1, true);
  (void)rmt_wait_tx_done(txChannel, pdMS_TO_TICKS(rxTimeoutMs));

  uint8_t presence = 0U;
  size_t rxSize = 0U;
  void* raw = xRingbufferReceive(rb, &rxSize, pdMS_TO_TICKS(rxTimeoutMs));
  if(raw != nullptr) {
    const rmt_item32_t* items = static_cast<rmt_item32_t*>(raw);
    const size_t count = rxSize / sizeof(rmt_item32_t);
    // After the master's long reset low, the slave answers with a presence low pulse. Detect any
    // low pulse of presence length that is shorter than the master's reset low (i.e. the response).
    for(size_t i = 0U; i < count; ++i) {
      if((items[i].level0 == 0U) && (items[i].duration0 >= presenceMinUs) &&
         (items[i].duration0 <= presenceMaxUs) && (items[i].duration0 < resetLowUs)) {
        presence = 1U;
        break;
      }
      if((items[i].level1 == 0U) && (items[i].duration1 >= presenceMinUs) &&
         (items[i].duration1 <= presenceMaxUs) && (items[i].duration1 < resetLowUs)) {
        presence = 1U;
        break;
      }
    }
    vRingbufferReturnItem(rb, raw);
  }
  (void)rmt_rx_stop(rxChannel);
  return presence;
}

void OneWire::write(uint8_t v, uint8_t power) {
  (void)power;  // RMT cannot assert a strong pull-up; rely on the external resistor.
  for(uint8_t bitMask = 0x01U; bitMask != 0U; bitMask <<= 1U) {
    write_bit((v & bitMask) ? 1U : 0U);
  }
}

void OneWire::write_bytes(const uint8_t* buf, uint16_t count, bool power) {
  (void)power;
  for(uint16_t i = 0U; i < count; ++i) {
    write(buf[i]);
  }
}

uint8_t OneWire::read(void) {
  uint8_t r = 0U;
  for(uint8_t bitMask = 0x01U; bitMask != 0U; bitMask <<= 1U) {
    if(read_bit()) { r |= bitMask; }
  }
  return r;
}

void OneWire::read_bytes(uint8_t* buf, uint16_t count) {
  for(uint16_t i = 0U; i < count; ++i) {
    buf[i] = read();
  }
}

void OneWire::select(const uint8_t rom[8]) {
  write(0x55U);  // Match ROM.
  for(uint8_t i = 0U; i < 8U; ++i) {
    write(rom[i]);
  }
}

void OneWire::skip(void) {
  write(0xCCU);  // Skip ROM.
}

void OneWire::depower(void) {
  // No strong pull-up to release on the RMT driver; nothing to do.
}

#if ONEWIRE_SEARCH
void OneWire::reset_search() {
  LastDiscrepancy = 0U;
  LastFamilyDiscrepancy = 0U;
  LastDeviceFlag = false;
  for(int8_t i = 7; i >= 0; --i) {
    ROM_NO[i] = 0U;
  }
}

void OneWire::target_search(uint8_t family_code) {
  ROM_NO[0] = family_code;
  for(uint8_t i = 1U; i < 8U; ++i) {
    ROM_NO[i] = 0U;
  }
  LastDiscrepancy = 64U;
  LastFamilyDiscrepancy = 0U;
  LastDeviceFlag = false;
}

uint8_t OneWire::search(uint8_t* newAddr, bool search_mode) {
  // Standard Maxim ROM search algorithm (same as paulstoffregen/OneWire).
  uint8_t id_bit_number = 1U;
  uint8_t last_zero = 0U;
  uint8_t rom_byte_number = 0U;
  uint8_t rom_byte_mask = 1U;
  uint8_t search_result = 0U;
  uint8_t id_bit = 0U;
  uint8_t cmp_id_bit = 0U;
  uint8_t search_direction = 0U;

  if(!LastDeviceFlag) {
    if(!reset()) {
      LastDiscrepancy = 0U;
      LastFamilyDiscrepancy = 0U;
      LastDeviceFlag = false;
      return 0U;
    }
    write(search_mode ? 0xF0U : 0xECU);

    do {
      id_bit = read_bit();
      cmp_id_bit = read_bit();

      if((id_bit == 1U) && (cmp_id_bit == 1U)) {
        break;  // No devices on the bus.
      }
      if(id_bit != cmp_id_bit) {
        search_direction = id_bit;  // All devices agree on this bit.
      } else {
        if(id_bit_number < LastDiscrepancy) {
          search_direction = ((ROM_NO[rom_byte_number] & rom_byte_mask) > 0U) ? 1U : 0U;
        } else {
          search_direction = (id_bit_number == LastDiscrepancy) ? 1U : 0U;
        }
        if(search_direction == 0U) {
          last_zero = id_bit_number;
          if(last_zero < 9U) {
            LastFamilyDiscrepancy = last_zero;
          }
        }
      }

      if(search_direction == 1U) {
        ROM_NO[rom_byte_number] |= rom_byte_mask;
      } else {
        ROM_NO[rom_byte_number] &= static_cast<uint8_t>(~rom_byte_mask);
      }

      write_bit(search_direction);

      id_bit_number++;
      rom_byte_mask <<= 1U;
      if(rom_byte_mask == 0U) {
        rom_byte_number++;
        rom_byte_mask = 1U;
      }
    } while(rom_byte_number < 8U);

    if(id_bit_number >= 65U) {
      LastDiscrepancy = last_zero;
      if(LastDiscrepancy == 0U) {
        LastDeviceFlag = true;
      }
      search_result = 1U;
    }
  }

  if((search_result == 0U) || (ROM_NO[0] == 0U)) {
    LastDiscrepancy = 0U;
    LastFamilyDiscrepancy = 0U;
    LastDeviceFlag = false;
    search_result = 0U;
  } else {
    for(uint8_t i = 0U; i < 8U; ++i) {
      newAddr[i] = ROM_NO[i];
    }
  }
  return search_result;
}
#endif  // ONEWIRE_SEARCH

#if ONEWIRE_CRC
uint8_t OneWire::crc8(const uint8_t* addr, uint8_t len) {
  uint8_t crc = 0U;
  while(len-- != 0U) {
    uint8_t inbyte = *addr++;
    for(uint8_t i = 8U; i != 0U; --i) {
      const uint8_t mix = (crc ^ inbyte) & 0x01U;
      crc >>= 1U;
      if(mix != 0U) { crc ^= 0x8CU; }
      inbyte >>= 1U;
    }
  }
  return crc;
}

#if ONEWIRE_CRC16
uint16_t OneWire::crc16(const uint8_t* input, uint16_t len, uint16_t crc) {
  static const uint8_t oddparity[16] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};
  for(uint16_t i = 0U; i < len; ++i) {
    uint16_t cdata = input[i];
    cdata = (cdata ^ crc) & 0xffU;
    crc >>= 8U;
    if((oddparity[cdata & 0x0FU] ^ oddparity[cdata >> 4U]) != 0U) {
      crc ^= 0xC001U;
    }
    cdata <<= 6U;
    crc ^= cdata;
    cdata <<= 1U;
    crc ^= cdata;
  }
  return crc;
}

bool OneWire::check_crc16(const uint8_t* input, uint16_t len, const uint8_t* inverted_crc, uint16_t crc) {
  crc = ~crc16(input, len, crc);
  return (crc & 0xFFU) == inverted_crc[0] && (crc >> 8U) == inverted_crc[1];
}
#endif  // ONEWIRE_CRC16
#endif  // ONEWIRE_CRC
