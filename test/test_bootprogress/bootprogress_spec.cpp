#include "bootProgress.hpp"
#include "BDDTest.h"
#include <string.h>

// What the startup progress record carries across a reset: how far the previous run got, how far
// this one has, and what a record this firmware cannot interpret is reported as.

bool test_the_first_run_has_nothing_on_record() {
  IT("a device whose store is empty reports an unknown previous stage");
  // The host build starts with an uninitialised store, which is what a power cycle leaves behind.
  BootProgress::begin();
  IS_EQUAL(static_cast<uint8_t>(BootProgress::getPrevious()), static_cast<uint8_t>(BootStage::Unknown));
  END_IT
}

bool test_begin_marks_the_run_as_started() {
  IT("begin() leaves the current stage at the banner");
  BootProgress::begin();
  IS_EQUAL(static_cast<uint8_t>(BootProgress::getCurrent()), static_cast<uint8_t>(BootStage::Banner));
  END_IT
}

bool test_the_last_stage_reached_survives_into_the_next_run() {
  IT("the stage the previous run reached is what the next one reports");
  BootProgress::set(BootStage::BrokerConnect);
  BootProgress::begin();   // Stands in for the reset: the store keeps its contents.
  IS_EQUAL(static_cast<uint8_t>(BootProgress::getPrevious()), static_cast<uint8_t>(BootStage::BrokerConnect));
  END_IT
}

bool test_each_step_replaces_the_last() {
  IT("set() moves the current stage on, and the newest one is what carries over");
  BootProgress::set(BootStage::ClockSync);
  IS_EQUAL(static_cast<uint8_t>(BootProgress::getCurrent()), static_cast<uint8_t>(BootStage::ClockSync));
  BootProgress::set(BootStage::Running);
  BootProgress::begin();
  IS_EQUAL(static_cast<uint8_t>(BootProgress::getPrevious()), static_cast<uint8_t>(BootStage::Running));
  END_IT
}

bool test_a_stage_this_firmware_does_not_know_reads_as_unknown() {
  IT("a record past the known stages is reported as unknown rather than mislabelled");
  // A downgrade can find a record written by firmware that knew more stages than this build.
  BootProgress::set(static_cast<BootStage>(200U));
  BootProgress::begin();
  IS_EQUAL(static_cast<uint8_t>(BootProgress::getPrevious()), static_cast<uint8_t>(BootStage::Unknown));
  END_IT
}

bool test_every_stage_has_its_own_name() {
  IT("each stage maps to a distinct name, and an invalid one falls back to UNKNOWN");
  IS_EQUAL(strcmp(BootProgress::getName(BootStage::AddressWait), "ADDRESS_WAIT"), 0);
  IS_EQUAL(strcmp(BootProgress::getName(BootStage::BrokerConnect), "BROKER_CONNECT"), 0);
  IS_EQUAL(strcmp(BootProgress::getName(BootStage::Running), "RUNNING"), 0);
  IS_EQUAL(strcmp(BootProgress::getName(static_cast<BootStage>(200U)), "UNKNOWN"), 0);
  END_IT
}

int main() {
  SUITE("BootProgress");
  test_the_first_run_has_nothing_on_record();
  test_begin_marks_the_run_as_started();
  test_the_last_stage_reached_survives_into_the_next_run();
  test_each_step_replaces_the_last();
  test_a_stage_this_firmware_does_not_know_reads_as_unknown();
  test_every_stage_has_its_own_name();
  FINISH
}
