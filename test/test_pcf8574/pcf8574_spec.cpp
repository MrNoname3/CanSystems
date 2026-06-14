#include "pcf8574.hpp"
#include "Arduino.h"
#include "Wire.h"
#include "BDDTest.h"

static constexpr uint8_t kAddr = 0x27U;
static constexpr uint32_t kTimeout = 5000U;

// PCF8574 deletes its copy/move constructors (it is a Task), so it cannot be returned from a
// helper; each test constructs it inline. Call before constructing to clear the shared shim bus
// and make endTransmission() ACK (device present).
static void resetBusAck() {
  Wire.reset();
  Wire.setEndTransmissionResult(0U);
}

// ---- construction / init ----

bool test_register_starts_all_high() {
  IT("a freshly constructed expander caches 0xFF (all pins high / inputs)");
  Wire.reset();
  PCF8574 pcf(kTimeout, kAddr);
  IS_EQUAL(pcf.getRegisterValue(), 0xFFU);
  END_IT
}

bool test_init_succeeds_when_device_acks() {
  IT("init() returns true when the device ACKs on the I2C bus");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  IS_TRUE(pcf.init());
  END_IT
}

bool test_init_fails_when_device_nacks() {
  IT("init() returns false when the device does not ACK");
  Wire.reset();
  Wire.setEndTransmissionResult(2U);  // NACK on address
  PCF8574 pcf(kTimeout, kAddr);
  IS_FALSE(pcf.init());
  END_IT
}

// ---- write() ----

bool test_write_fails_before_init() {
  IT("write() fails when the device was never initialised");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);  // no init() -> deviceExists == false
  IS_FALSE(pcf.write(0x0FU));
  END_IT
}

bool test_write_updates_cached_register() {
  IT("a successful write() caches the written register value");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();
  IS_TRUE(pcf.write(0xA5U));
  IS_EQUAL(pcf.getRegisterValue(), 0xA5U);
  END_IT
}

bool test_write_failure_keeps_old_register() {
  IT("write() that NACKs returns false and leaves the cached register unchanged");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();
  IS_TRUE(pcf.write(0xA5U));
  Wire.setEndTransmissionResult(1U);  // next endTransmission NACKs
  IS_FALSE(pcf.write(0x5AU));
  IS_EQUAL(pcf.getRegisterValue(), 0xA5U);
  END_IT
}

// ---- read() ----

bool test_read_fails_before_init() {
  IT("read() fails when the device was never initialised");
  Wire.reset();
  PCF8574 pcf(kTimeout, kAddr);
  uint8_t value = 0U;
  IS_FALSE(pcf.read(value));
  END_IT
}

bool test_read_returns_queued_byte() {
  IT("read() returns the byte presented on the bus");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();
  Wire.addReadByte(0x3CU);
  uint8_t value = 0U;
  IS_TRUE(pcf.read(value));
  IS_EQUAL(value, 0x3CU);
  END_IT
}

bool test_read_fails_when_no_data() {
  IT("read() fails when the device returns no bytes");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();  // no bytes queued -> requestFrom returns 0
  uint8_t value = 0U;
  IS_FALSE(pcf.read(value));
  END_IT
}

// ---- digitalWrite() ----

bool test_digital_write_rejects_out_of_range_pin() {
  IT("digitalWrite() rejects pins above 7");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();
  IS_FALSE(pcf.digitalWrite(8U, 1U));
  END_IT
}

bool test_digital_write_low_clears_bit() {
  IT("digitalWrite(pin, 0) clears only that pin's bit (0xFF -> 0xFB for pin 2)");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();  // register starts at 0xFF
  IS_TRUE(pcf.digitalWrite(2U, 0U));
  IS_EQUAL(pcf.getRegisterValue(), 0xFBU);  // bit 2 cleared
  END_IT
}

bool test_digital_write_high_sets_bit() {
  IT("digitalWrite(pin, 1) sets only that pin's bit");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();
  IS_TRUE(pcf.write(0x00U));            // start from all-low
  IS_TRUE(pcf.digitalWrite(5U, 1U));
  IS_EQUAL(pcf.getRegisterValue(), 0x20U);  // only bit 5 set
  END_IT
}

bool test_digital_write_preserves_other_bits() {
  IT("digitalWrite() leaves the other pins untouched");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();
  IS_TRUE(pcf.write(0b10100101U));
  IS_TRUE(pcf.digitalWrite(1U, 1U));   // set bit 1
  IS_EQUAL(pcf.getRegisterValue(), 0b10100111U);
  END_IT
}

// ---- setAsInput() ----

bool test_set_as_input_drives_pin_high() {
  IT("setAsInput() sets the pin high (pull-up input on a PCF8574)");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();
  IS_TRUE(pcf.write(0x00U));
  IS_TRUE(pcf.setAsInput(3U));
  IS_EQUAL(pcf.getRegisterValue(), 0x08U);  // bit 3 set
  END_IT
}

// ---- digitalRead() ----

bool test_digital_read_extracts_bit() {
  IT("digitalRead() returns 1 when the bus byte has that pin set");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();
  Wire.addReadByte(0b00010000U);  // bit 4 set
  IS_EQUAL(pcf.digitalRead(4U), 1U);
  END_IT
}

bool test_digital_read_zero_bit() {
  IT("digitalRead() returns 0 when the bus byte has that pin clear");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();
  Wire.addReadByte(0b00010000U);  // bit 4 set, bit 0 clear
  IS_EQUAL(pcf.digitalRead(0U), 0U);
  END_IT
}

bool test_digital_read_out_of_range_returns_255() {
  IT("digitalRead() returns 255 for pins above 7");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();
  Wire.addReadByte(0xFFU);
  IS_EQUAL(pcf.digitalRead(8U), 255U);
  END_IT
}

bool test_digital_read_failure_returns_255() {
  IT("digitalRead() returns 255 when the bus read fails");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();  // no bytes queued
  IS_EQUAL(pcf.digitalRead(0U), 255U);
  END_IT
}

// ---- toggleState() ----

bool test_toggle_flips_bit() {
  IT("toggleState() flips the cached pin bit on each call");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();
  IS_TRUE(pcf.write(0x00U));
  IS_TRUE(pcf.toggleState(0U));
  IS_EQUAL(pcf.getRegisterValue(), 0x01U);
  IS_TRUE(pcf.toggleState(0U));
  IS_EQUAL(pcf.getRegisterValue(), 0x00U);
  END_IT
}

bool test_toggle_rejects_out_of_range_pin() {
  IT("toggleState() rejects pins above 7");
  resetBusAck();
  PCF8574 pcf(kTimeout, kAddr);
  (void)pcf.init();
  IS_FALSE(pcf.toggleState(8U));
  END_IT
}

int main() {
  SUITE("PCF8574");
  test_register_starts_all_high();
  test_init_succeeds_when_device_acks();
  test_init_fails_when_device_nacks();
  test_write_fails_before_init();
  test_write_updates_cached_register();
  test_write_failure_keeps_old_register();
  test_read_fails_before_init();
  test_read_returns_queued_byte();
  test_read_fails_when_no_data();
  test_digital_write_rejects_out_of_range_pin();
  test_digital_write_low_clears_bit();
  test_digital_write_high_sets_bit();
  test_digital_write_preserves_other_bits();
  test_set_as_input_drives_pin_high();
  test_digital_read_extracts_bit();
  test_digital_read_zero_bit();
  test_digital_read_out_of_range_returns_255();
  test_digital_read_failure_returns_255();
  test_toggle_flips_bit();
  test_toggle_rejects_out_of_range_pin();
  FINISH
}
