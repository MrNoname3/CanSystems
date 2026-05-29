#pragma once
//
// RMT-backed, OneWire-API-compatible 1-Wire driver for ESP32 (drop-in for paulstoffregen/OneWire).
//
// Why: the classic bit-bang OneWire drives the bus by toggling the GPIO in software with interrupts
// disabled (portENTER_CRITICAL / noInterrupts) for microsecond accuracy — bad for WiFi/other ISRs.
// The ESP32 RMT peripheral generates and samples precise pulse trains in hardware, so the timing is
// offloaded: no interrupt lockout, and the calling task blocks on a FreeRTOS wait (RMT TX/RX done)
// instead of spinning. The public API matches paulstoffregen/OneWire so DallasTemperature and our
// Ds18b20Reader use it unchanged.
//
// Targets the legacy RMT driver (driver/rmt.h), i.e. arduino-esp32 2.0.x / ESP-IDF 4.4. The new
// IDF 5.x RMT encoder API would need a rewrite — see notes in OneWire.cpp.
//
// STATUS: compiles and is structured after the proven esp32-owb (owb_rmt) timing; the on-wire timing
// constants and RX decode thresholds still need validation against real DS18B20 hardware + a scope.
//
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <Arduino.h>                                                /// Arduino types (used by the OneWire API).
#if !defined(ESP32)
#error "oneWireRmt is ESP32-only (uses the RMT peripheral)."
#endif
#include "driver/rmt.h"                                             /// Legacy RMT driver (IDF 4.4).

// Feature switches kept for source compatibility with the original OneWire library.
#ifndef ONEWIRE_SEARCH
#define ONEWIRE_SEARCH 1
#endif
#ifndef ONEWIRE_CRC
#define ONEWIRE_CRC 1
#endif
#ifndef ONEWIRE_CRC8_TABLE
#define ONEWIRE_CRC8_TABLE 1
#endif
#ifndef ONEWIRE_CRC16
#define ONEWIRE_CRC16 1
#endif

/// @brief Drop-in replacement for the Arduino `OneWire` class, driven by the ESP32 RMT peripheral.
class OneWire {
public:
  /// @brief Default constructor; call begin() before use.
  OneWire() = default;

  /// @brief Constructs and initializes the bus on the given pin.
  /// @param pin GPIO connected to the 1-Wire data line (external 4.7k pull-up required).
  explicit OneWire(uint8_t pin) { begin(pin); }

  /// @brief Releases the RMT channels.
  ~OneWire();

  /// @brief Initializes the RMT TX/RX channels and the open-drain bus pin.
  /// @param pin GPIO connected to the 1-Wire data line.
  void begin(uint8_t pin);

  /// @brief Issues a 1-Wire reset and samples for a presence pulse.
  /// @return 1 if at least one device responded, 0 otherwise.
  uint8_t reset(void);

  /// @brief Issues a ROM-select command (0x55) followed by the 8-byte ROM address.
  void select(const uint8_t rom[8]);

  /// @brief Issues a skip-ROM command (0xCC).
  void skip(void);

  /// @brief Writes a byte, LSB first.
  /// @param v Byte to write.
  /// @param power Unused on RMT (kept for API compatibility); the external pull-up powers the bus.
  void write(uint8_t v, uint8_t power = 0);

  /// @brief Writes a buffer of bytes.
  void write_bytes(const uint8_t* buf, uint16_t count, bool power = 0);

  /// @brief Reads a byte, LSB first.
  uint8_t read(void);

  /// @brief Reads a buffer of bytes.
  void read_bytes(uint8_t* buf, uint16_t count);

  /// @brief Writes a single bit.
  void write_bit(uint8_t v);

  /// @brief Reads a single bit.
  uint8_t read_bit(void);

  /// @brief No-op on RMT (no strong pull-up to release); kept for API compatibility.
  void depower(void);

#if ONEWIRE_SEARCH
  /// @brief Resets the search state so the next search() starts from the first device.
  void reset_search();

  /// @brief Primes a search for devices of a given family code.
  void target_search(uint8_t family_code);

  /// @brief Standard Maxim ROM search.
  /// @param newAddr Output buffer (8 bytes) receiving the next ROM address.
  /// @param search_mode true: normal search (0xF0); false: alarm search (0xEC).
  /// @return 1 if a device was found (address in newAddr), 0 if no more devices.
  uint8_t search(uint8_t* newAddr, bool search_mode = true);
#endif

#if ONEWIRE_CRC
  /// @brief Computes the Maxim/Dallas CRC-8 over a buffer.
  static uint8_t crc8(const uint8_t* addr, uint8_t len);

#if ONEWIRE_CRC16
  /// @brief Verifies a CRC-16 against an inverted CRC pair.
  static bool check_crc16(const uint8_t* input, uint16_t len, const uint8_t* inverted_crc, uint16_t crc = 0);

  /// @brief Computes the Maxim/Dallas CRC-16 over a buffer.
  static uint16_t crc16(const uint8_t* input, uint16_t len, uint16_t crc = 0);
#endif
#endif

  OneWire(const OneWire&) = delete;                                 // Define copy constructor.
  OneWire& operator=(const OneWire&) = delete;                      // Define copy assignment operator.
  OneWire(OneWire&&) = delete;                                      // Define move constructor.
  OneWire& operator=(OneWire&&) = delete;                           // Define move assignment operator.

private:
  /// @brief Sends a batch of RMT TX items in one transaction and waits for completion.
  /// @param items Pointer to the items (one per 1-Wire bit slot).
  /// @param count Number of items (1..8).
  void txItems(const rmt_item32_t* items, int count);

  /// @brief Drives `count` read time-slots in one RMT transaction and decodes the sampled bits.
  /// @param outBits Output buffer receiving `count` bits (1 or 0); defaults to 1 on missing data.
  /// @param count Number of slots/bits to read (1..8).
  void readSlots(uint8_t* outBits, uint8_t count);

  gpio_num_t busPin = GPIO_NUM_NC;                                  // 1-Wire data pin.
  bool initialized = false;                                         // Whether begin() succeeded.

#if ONEWIRE_SEARCH
  uint8_t ROM_NO[8] = {0};                                          // Current/last found ROM address.
  uint8_t LastDiscrepancy = 0;                                      // Search bookkeeping (Maxim algorithm).
  uint8_t LastFamilyDiscrepancy = 0;                                // Search bookkeeping.
  bool LastDeviceFlag = false;                                      // Set once the last device was returned.
#endif
};
