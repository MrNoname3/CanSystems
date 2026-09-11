// The real SPIFlash driver against a flash model on the shim SPI bus. Most suites replace this
// driver with test/_shims/src/SPIFlash.h, so the library is lib_ignored for native_test and the
// source is pulled in here by path: this is the only suite that drives the real one.
#include "../../lib/SPIFlash/src/SPIFlash.cpp"   // NOLINT(bugprone-suspicious-include)
#include "SPI.h"
#include "Arduino.h"
#include "BDDTest.h"
#include <string.h>

namespace {
  constexpr uint8_t kSelectPin = 8U;               // What the CAN nodes wire the flash to.
  constexpr uint16_t kJedecId = 0xEF30U;           // Winbond W25X40CL.

  SpiFlashModel flash;

  /// @brief A fresh bus with an erased flash attached to it.
  void prepareBus() {
    resetGpioState();
    flash.reset();
    flash.setJedecId(kJedecId);
    attachSpiFlash(&flash, kSelectPin);
  }

  /// @brief Bytes 0, 1, 2 … so a misplaced piece of a write shows up as the wrong value.
  void fillCounting(uint8_t* buffer, uint16_t length) {
    for(uint16_t i = 0U; i < length; i++) { buffer[i] = static_cast<uint8_t>(i); }
  }
} // namespace

bool test_a_write_inside_one_page_is_a_single_transaction() {
  IT("a write that stays inside one page goes out as one transaction");
  prepareBus();
  SPIFlash driver(kSelectPin, kJedecId);
  IS_TRUE(driver.initialize());
  uint8_t data[16] = {};
  fillCounting(data, sizeof(data));
  IS_TRUE(driver.writeBytes(0x001010U, data, sizeof(data)));
  IS_EQUAL(flash.getPrograms().size(), 1U);
  IS_EQUAL(flash.getPrograms()[0].address, 0x001010U);
  IS_EQUAL(flash.getPrograms()[0].length, 16U);
  IS_TRUE(flash.holds(0x001010U, data, sizeof(data)));
  END_IT
}

bool test_a_write_crossing_a_page_boundary_is_split_at_it() {
  IT("a write crossing a page boundary is split at the boundary, not at 256 bytes from the start");
  prepareBus();
  SPIFlash driver(kSelectPin, kJedecId);
  IS_TRUE(driver.initialize());
  uint8_t data[300] = {};
  fillCounting(data, sizeof(data));
  // Starts 2 bytes before the end of page 0: the first piece is those 2 bytes, then a full page,
  // then the remainder. A write that counted 256 from the start instead would run over the end
  // of page 1, where the chip wraps back to its beginning and overwrites what it just took.
  IS_TRUE(driver.writeBytes(0x0000FEU, data, sizeof(data)));
  IS_EQUAL(flash.getPrograms().size(), 3U);
  IS_EQUAL(flash.getPrograms()[0].address, 0x0000FEU);
  IS_EQUAL(flash.getPrograms()[0].length, 2U);
  IS_EQUAL(flash.getPrograms()[1].address, 0x000100U);
  IS_EQUAL(flash.getPrograms()[1].length, 256U);
  IS_EQUAL(flash.getPrograms()[2].address, 0x000200U);
  IS_EQUAL(flash.getPrograms()[2].length, 42U);
  IS_TRUE(flash.holds(0x0000FEU, data, sizeof(data)));
  END_IT
}

bool test_a_page_aligned_write_of_a_whole_page_is_not_split() {
  IT("a page-aligned write of exactly one page is not split");
  prepareBus();
  SPIFlash driver(kSelectPin, kJedecId);
  IS_TRUE(driver.initialize());
  uint8_t data[256] = {};
  fillCounting(data, sizeof(data));
  IS_TRUE(driver.writeBytes(0x000300U, data, sizeof(data)));
  IS_EQUAL(flash.getPrograms().size(), 1U);
  IS_EQUAL(flash.getPrograms()[0].length, 256U);
  IS_TRUE(flash.holds(0x000300U, data, sizeof(data)));
  END_IT
}

bool test_a_write_ending_on_a_page_boundary_takes_no_extra_transaction() {
  IT("a write ending exactly on a page boundary takes no further transaction");
  prepareBus();
  SPIFlash driver(kSelectPin, kJedecId);
  IS_TRUE(driver.initialize());
  uint8_t data[6] = {};
  fillCounting(data, sizeof(data));
  IS_TRUE(driver.writeBytes(0x0004FAU, data, sizeof(data)));
  IS_EQUAL(flash.getPrograms().size(), 1U);
  IS_EQUAL(flash.getPrograms()[0].length, 6U);
  END_IT
}

bool test_a_write_of_nothing_does_not_reach_the_chip() {
  IT("a write of no bytes issues no transaction");
  prepareBus();
  SPIFlash driver(kSelectPin, kJedecId);
  IS_TRUE(driver.initialize());
  const uint8_t data[1] = { 0xA5U };
  IS_TRUE(driver.writeBytes(0x000000U, data, 0U));
  IS_EQUAL(flash.getPrograms().size(), 0U);
  END_IT
}

bool test_every_piece_of_a_split_write_is_armed_on_its_own() {
  IT("every piece of a split write arms the write-enable latch of its own");
  prepareBus();
  SPIFlash driver(kSelectPin, kJedecId);
  IS_TRUE(driver.initialize());
  uint8_t data[600] = {};
  fillCounting(data, sizeof(data));
  // The latch clears with each program the chip takes, so a driver that armed it once would
  // have its second and third pieces ignored by the chip. Every piece landing proves it did not.
  IS_TRUE(driver.writeBytes(0x0000FFU, data, sizeof(data)));
  IS_EQUAL(flash.getPrograms().size(), 4U);
  IS_TRUE(flash.holds(0x0000FFU, data, sizeof(data)));
  IS_FALSE(flash.isWriteEnabled());
  END_IT
}

bool test_a_split_write_comes_back_the_way_it_went_in() {
  IT("a split write reads back as one run of bytes");
  prepareBus();
  SPIFlash driver(kSelectPin, kJedecId);
  IS_TRUE(driver.initialize());
  uint8_t data[513] = {};
  fillCounting(data, sizeof(data));
  IS_TRUE(driver.writeBytes(0x000080U, data, sizeof(data)));
  uint8_t readBack[513] = {};
  IS_TRUE(driver.readBytes(0x000080U, readBack, sizeof(readBack)));
  IS_EQUAL(memcmp(readBack, data, sizeof(data)), 0);
  END_IT
}

bool test_a_chip_that_stays_busy_fails_the_write() {
  IT("a chip that never comes ready fails the write instead of reporting success");
  prepareBus();
  SPIFlash driver(kSelectPin, kJedecId);
  IS_TRUE(driver.initialize());
  setFakeMillis(1000U);
  flash.setNotResponding(true);
  uint8_t data[8] = {};
  fillCounting(data, sizeof(data));
  IS_FALSE(driver.writeBytes(0x000600U, data, sizeof(data)));
  IS_EQUAL(flash.getPrograms().size(), 0U);
  flash.setNotResponding(false);
  clearFakeMillis();
  END_IT
}

bool test_a_write_waits_out_a_busy_chip_between_pages() {
  IT("a split write waits out a chip that is still busy from the previous page");
  prepareBus();
  SPIFlash driver(kSelectPin, kJedecId);
  IS_TRUE(driver.initialize());
  uint8_t data[300] = {};
  fillCounting(data, sizeof(data));
  flash.setBusyReads(3U);                       // Busy for the first few polls, then ready.
  IS_TRUE(driver.writeBytes(0x000700U, data, sizeof(data)));
  IS_EQUAL(flash.getPrograms().size(), 2U);
  IS_TRUE(flash.holds(0x000700U, data, sizeof(data)));
  END_IT
}

bool test_initialize_refuses_a_chip_with_another_id() {
  IT("initialize() refuses a chip whose JEDEC id is not the one asked for");
  resetGpioState();
  flash.reset();
  flash.setJedecId(0x1F44U);                    // Atmel-Adesto AT25DF041A, not what the driver wants.
  attachSpiFlash(&flash, kSelectPin);
  SPIFlash driver(kSelectPin, kJedecId);
  IS_FALSE(driver.initialize());
  END_IT
}

int main() {
  SUITE("SPIFlash");

  test_a_write_inside_one_page_is_a_single_transaction();
  test_a_write_crossing_a_page_boundary_is_split_at_it();
  test_a_page_aligned_write_of_a_whole_page_is_not_split();
  test_a_write_ending_on_a_page_boundary_takes_no_extra_transaction();
  test_a_write_of_nothing_does_not_reach_the_chip();
  test_every_piece_of_a_split_write_is_armed_on_its_own();
  test_a_split_write_comes_back_the_way_it_went_in();
  test_a_chip_that_stays_busy_fails_the_write();
  test_a_write_waits_out_a_busy_chip_between_pages();
  test_initialize_refuses_a_chip_with_another_id();
  FINISH
}
