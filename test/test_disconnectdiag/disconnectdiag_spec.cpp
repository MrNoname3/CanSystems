#include "disconnectDiag.hpp"
#include "BDDTest.h"
#include <string.h>

// The bookkeeping behind the [DIAG] retained message: what took the device offline, for how
// long it stayed there, and how many times it has come back since boot.

static constexpr const char kMqttCause[] = "CONNECTION_LOST";
static constexpr const char kDropTime[] = "2026-08-30T09:15:00Z";

bool test_nothing_is_reported_before_the_first_outage() {
  IT("a device that has never dropped reports nothing on its first connect");
  DisconnectDiag diag;
  DisconnectDiag::Report report;
  // The first connect after boot must not publish diagnostics for an outage that never happened.
  IS_FALSE(diag.takeReport(1000U, report));
  END_IT
}

bool test_a_recorded_outage_is_reported_once_it_ends() {
  IT("a recorded outage reports its cause, its drop time and how long it lasted");
  DisconnectDiag diag;
  diag.recordDisconnect(true, kMqttCause, kDropTime, 10000U);
  DisconnectDiag::Report report;
  IS_TRUE(diag.takeReport(73000U, report));
  IS_EQUAL(strcmp(report.cause, kMqttCause), 0);
  IS_EQUAL(strcmp(report.dropTime, kDropTime), 0);
  IS_EQUAL(report.offlineSeconds, 63U);
  IS_EQUAL(report.reconnectCount, 1U);
  END_IT
}

bool test_a_lost_link_outranks_whatever_mqtt_reported() {
  IT("a dropped network link is recorded as the cause, not the MQTT state it tore down");
  DisconnectDiag diag;
  // MQTT reports a failure because the link vanished underneath it; the link is the root cause.
  diag.recordDisconnect(false, kMqttCause, kDropTime, 10000U);
  DisconnectDiag::Report report;
  IS_TRUE(diag.takeReport(11000U, report));
  IS_EQUAL(strcmp(report.cause, DisconnectDiag::getNetworkLostCause()), 0);
  END_IT
}

bool test_an_outage_is_reported_only_once() {
  IT("a second connect without a new outage in between reports nothing");
  DisconnectDiag diag;
  diag.recordDisconnect(true, kMqttCause, kDropTime, 10000U);
  DisconnectDiag::Report report;
  IS_TRUE(diag.takeReport(11000U, report));
  IS_FALSE(diag.takeReport(12000U, report));       // the record was consumed
  END_IT
}

bool test_the_reconnect_count_grows_with_each_outage() {
  IT("the reconnect count counts outages that were reported, and only those");
  DisconnectDiag diag;
  DisconnectDiag::Report report;
  diag.recordDisconnect(true, kMqttCause, kDropTime, 1000U);
  IS_TRUE(diag.takeReport(2000U, report));
  IS_EQUAL(report.reconnectCount, 1U);
  IS_FALSE(diag.takeReport(3000U, report));        // no outage: must not count
  diag.recordDisconnect(false, kMqttCause, kDropTime, 4000U);
  IS_TRUE(diag.takeReport(5000U, report));
  IS_EQUAL(report.reconnectCount, 2U);
  END_IT
}

bool test_the_offline_duration_survives_the_millis_wrap() {
  IT("an outage spanning the millis() wrap still reports its real length");
  DisconnectDiag diag;
  // millis() wraps every ~49 days; an outage across that point must not report a huge duration.
  diag.recordDisconnect(true, kMqttCause, kDropTime, 0xFFFFF000UL);
  DisconnectDiag::Report report;
  IS_TRUE(diag.takeReport(0x00000FA0UL, report));   // 8096 ms later, across the wrap
  IS_EQUAL(report.offlineSeconds, 8U);
  END_IT
}

bool test_a_missing_drop_time_reports_an_empty_string() {
  IT("an outage recorded while the clock was unset reports an empty drop time, not a dangling one");
  DisconnectDiag diag;
  diag.recordDisconnect(true, kMqttCause, nullptr, 1000U);
  DisconnectDiag::Report report;
  IS_TRUE(diag.takeReport(2000U, report));
  IS_TRUE(report.dropTime != nullptr);
  IS_EQUAL(strlen(report.dropTime), 0U);
  END_IT
}

int main() {
  SUITE("DisconnectDiag");
  test_nothing_is_reported_before_the_first_outage();
  test_a_recorded_outage_is_reported_once_it_ends();
  test_a_lost_link_outranks_whatever_mqtt_reported();
  test_an_outage_is_reported_only_once();
  test_the_reconnect_count_grows_with_each_outage();
  test_the_offline_duration_survives_the_millis_wrap();
  test_a_missing_drop_time_reports_an_empty_string();
  FINISH
}
