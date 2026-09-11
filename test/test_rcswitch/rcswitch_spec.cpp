// The real RCSwitch driver against the shim's pulse recorder. Every other suite replaces the
// driver with test/_shims/src/RCSwitch.h, so the library is lib_ignored for native_test and the
// source is pulled in here by path: this is the only suite that sees what actually goes on the
// transmitter pin.
#include "../../lib/RCSwitch/src/RCSwitch.cpp"   // NOLINT(bugprone-suspicious-include)
#include "Arduino.h"
#include "BDDTest.h"

namespace {
  constexpr uint8_t kTxPin = 5U;                   // What the rad node wires the transmitter to.

  /// @brief Puts the driver on the transmitter pin and starts recording what it drives there.
  void prepare(RCSwitch& driver, int32_t protocolNumber) {
    resetGpioState();
    driver.enableTransmit(kTxPin);
    driver.setProtocol(protocolNumber);
    driver.setRepeatTransmit(1);
    recordPulsesOn(kTxPin);
  }

  /// @brief Duration of pulse `index`, or 0 when there is none.
  uint32_t pulseAt(uint32_t index) {
    return (index < recordedPulseCount()) ? recordedPulses()[index].microseconds : 0U;
  }

  /// @brief Level of pulse `index`, or 0xFF when there is none.
  uint8_t levelAt(uint32_t index) {
    return (index < recordedPulseCount()) ? recordedPulses()[index].level : 0xFFU;
  }
} // namespace

bool test_a_bit_goes_out_as_the_protocol_says() {
  IT("protocol 1 sends a one as 3:1 and a zero as 1:3 of its pulse length");
  RCSwitch driver;
  prepare(driver, 1);
  // Protocol 1: pulseLength 350, sync 1:31, zero 1:3, one 3:1, no preamble and no guard.
  driver.send(0b10U, 2U);
  stopRecordingPulses();
  // Header, then the two bits, then the line is left low: 4 edges per pulse pair plus the final
  // low. Each pulse is one high edge and one low edge.
  IS_EQUAL(recordedPulseCount(), 7U);
  IS_EQUAL(levelAt(0U), HIGH);
  IS_EQUAL(pulseAt(0U), 350U);                     // Header high: 1 x 350.
  IS_EQUAL(levelAt(1U), LOW);
  IS_EQUAL(pulseAt(1U), 31U * 350U);               // Header low: 31 x 350.
  IS_EQUAL(pulseAt(2U), 3U * 350U);                // One: high for 3.
  IS_EQUAL(pulseAt(3U), 350U);                     //      low for 1.
  IS_EQUAL(pulseAt(4U), 350U);                     // Zero: high for 1.
  IS_EQUAL(pulseAt(5U), 3U * 350U);                //       low for 3.
  IS_EQUAL(levelAt(6U), LOW);                      // The line is left low.
  END_IT
}

bool test_bits_go_out_most_significant_first() {
  IT("the code word goes out most significant bit first");
  RCSwitch driver;
  prepare(driver, 1);
  driver.send(0b1000U, 4U);
  stopRecordingPulses();
  // Header (2 edges), four bits (8 edges), the closing low.
  IS_EQUAL(recordedPulseCount(), 11U);
  IS_EQUAL(pulseAt(2U), 3U * 350U);                // First bit out is the 1.
  IS_EQUAL(pulseAt(4U), 350U);                     // The three zeros follow.
  IS_EQUAL(pulseAt(6U), 350U);
  IS_EQUAL(pulseAt(8U), 350U);
  END_IT
}

bool test_only_the_named_bits_are_sent() {
  IT("only as many bits as the length names are sent, whatever else the code word holds");
  RCSwitch driver;
  prepare(driver, 1);
  driver.send(0xFFFFFFFFU, 3U);
  stopRecordingPulses();
  IS_EQUAL(recordedPulseCount(), 2U + (3U * 2U) + 1U);
  END_IT
}

bool test_a_full_width_code_word_is_sent_whole() {
  IT("a 64-bit code word is sent whole, the top bit included");
  RCSwitch driver;
  prepare(driver, 1);
  // 64 is the width rfHandler caps a command at, because the bit test RCSwitch::send() does
  // (`code & (1ULL << i)`) is undefined past it. The top bit has to come out as a one.
  driver.send(1ULL << 63U, 64U);
  stopRecordingPulses();
  IS_EQUAL(recordedPulseCount(), 2U + (64U * 2U) + 1U);
  IS_EQUAL(pulseAt(2U), 3U * 350U);                // Bit 63 is the one.
  IS_EQUAL(pulseAt(4U), 350U);                     // Bit 62 is a zero.
  IS_EQUAL(pulseAt(2U + (63U * 2U)), 350U);        // So is bit 0.
  END_IT
}

bool test_a_repeat_sends_the_same_frame_again() {
  IT("a repeat count sends the whole frame that many times");
  resetGpioState();
  RCSwitch driver;
  driver.enableTransmit(kTxPin);
  driver.setProtocol(1);
  driver.setRepeatTransmit(3);
  recordPulsesOn(kTxPin);
  driver.send(0b1U, 1U);
  stopRecordingPulses();
  // Header plus one bit is 4 edges; three of those, then the single closing low.
  IS_EQUAL(recordedPulseCount(), (3U * 4U) + 1U);
  IS_EQUAL(pulseAt(0U), 350U);                     // Each repeat opens with the header again.
  IS_EQUAL(pulseAt(4U), 350U);
  IS_EQUAL(pulseAt(8U), 350U);
  END_IT
}

bool test_an_inverted_protocol_starts_low() {
  IT("an inverted protocol drives every pulse the other way round");
  RCSwitch driver;
  prepare(driver, 1);
  // The table this fork carries holds one protocol, and it is not inverted; a caller wanting
  // inversion brings its own description, which is what setProtocol(Protocol) is for.
  driver.setProtocol(RCSwitch::Protocol{ 350U, 0U, { 0U, 0U }, 1U, { 1U, 31U }, { 1U, 3U }, { 3U, 1U }, true, 0U });
  driver.send(0b1U, 1U);
  stopRecordingPulses();
  IS_EQUAL(levelAt(0U), LOW);                      // The header's first part is low now,
  IS_EQUAL(pulseAt(0U), 350U);                     // for the length its `high` field names.
  IS_EQUAL(levelAt(1U), HIGH);
  IS_EQUAL(pulseAt(1U), 31U * 350U);
  IS_EQUAL(levelAt(2U), LOW);                      // And so is the bit that follows.
  IS_EQUAL(pulseAt(2U), 3U * 350U);
  END_IT
}

bool test_a_preamble_is_sent_once_per_two_of_its_factor() {
  IT("the preamble goes out as half its factor, rounded up");
  RCSwitch driver;
  prepare(driver, 1);
  // A preamble pulse carries two of the factor's parts, so an odd factor still costs a whole
  // pulse: 3 means two pulses, the second of which is only half used.
  driver.setProtocol(RCSwitch::Protocol{ 350U, 3U, { 2U, 4U }, 1U, { 1U, 31U }, { 1U, 3U }, { 3U, 1U }, false, 0U });
  driver.send(0b1U, 1U);
  stopRecordingPulses();
  // Two preamble pulses (4 edges), the header (2), the bit (2), the closing low.
  IS_EQUAL(recordedPulseCount(), 9U);
  IS_EQUAL(pulseAt(0U), 2U * 350U);
  IS_EQUAL(pulseAt(1U), 4U * 350U);
  IS_EQUAL(pulseAt(2U), 2U * 350U);
  IS_EQUAL(pulseAt(3U), 4U * 350U);
  IS_EQUAL(pulseAt(4U), 350U);                     // Then the header.
  END_IT
}

bool test_the_pulse_length_can_be_overridden() {
  IT("setPulseLength() replaces the length the protocol brought with it");
  RCSwitch driver;
  prepare(driver, 1);
  driver.setPulseLength(100);
  driver.send(0b1U, 1U);
  stopRecordingPulses();
  IS_EQUAL(pulseAt(0U), 100U);                     // Header high: 1 x 100.
  IS_EQUAL(pulseAt(1U), 31U * 100U);               // Header low: 31 x 100.
  IS_EQUAL(pulseAt(2U), 3U * 100U);                // One: high for 3.
  END_IT
}

bool test_a_guard_time_holds_the_line_low_after_the_frame() {
  IT("a guard time holds the line low for its own length after the frame");
  RCSwitch driver;
  prepare(driver, 1);
  driver.setProtocol(RCSwitch::Protocol{ 350U, 0U, { 0U, 0U }, 1U, { 1U, 31U }, { 1U, 3U }, { 3U, 1U }, false, 5U });
  driver.send(0b1U, 1U);
  stopRecordingPulses();
  // Header (2 edges), the bit (2), the guard, then the closing low.
  IS_EQUAL(recordedPulseCount(), 6U);
  IS_EQUAL(levelAt(4U), LOW);
  IS_EQUAL(pulseAt(4U), 5U * 350U);
  END_IT
}

bool test_nothing_is_sent_without_a_transmitter_pin() {
  IT("send() does nothing until a transmitter pin is named");
  resetGpioState();
  RCSwitch driver;
  recordPulsesOn(kTxPin);
  driver.send(0xFFU, 8U);
  stopRecordingPulses();
  IS_EQUAL(recordedPulseCount(), 0U);
  END_IT
}

bool test_an_unknown_protocol_number_falls_back_to_the_first() {
  IT("a protocol number outside the table falls back to the first one");
  RCSwitch driver;
  prepare(driver, 1);
  // This fork carries one protocol; anything past it is out of the table.
  driver.setProtocol(static_cast<int32_t>(driver.getNumProtos()) + 1);
  driver.send(0b1U, 1U);
  stopRecordingPulses();
  IS_EQUAL(pulseAt(0U), 350U);                     // Protocol 1's header again.
  IS_EQUAL(pulseAt(1U), 31U * 350U);
  END_IT
}

int main() {
  SUITE("RCSwitch");

  test_a_bit_goes_out_as_the_protocol_says();
  test_bits_go_out_most_significant_first();
  test_only_the_named_bits_are_sent();
  test_a_full_width_code_word_is_sent_whole();
  test_a_repeat_sends_the_same_frame_again();
  test_an_inverted_protocol_starts_low();
  test_a_preamble_is_sent_once_per_two_of_its_factor();
  test_the_pulse_length_can_be_overridden();
  test_a_guard_time_holds_the_line_low_after_the_frame();
  test_nothing_is_sent_without_a_transmitter_pin();
  test_an_unknown_protocol_number_falls_back_to_the_first();
  FINISH
}
