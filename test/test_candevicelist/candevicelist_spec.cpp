#include "canDeviceList.hpp"
#include "BDDTest.h"

// A device stand-in: carries the client id the list routes on, plus the intrusive link.
struct FakeDevice {
  explicit FakeDevice(uint16_t id) :
    clientCanId(id) {}

  [[nodiscard]] uint16_t getClientCanId() const { return clientCanId; }
  [[nodiscard]] FakeDevice* getNextDevice() const { return next; }
  void setNextDevice(FakeDevice* device) { next = device; }

  uint16_t clientCanId;
  FakeDevice* next = nullptr;
};

bool test_an_empty_list_routes_nowhere() {
  IT("an empty list has no first device and finds nothing");
  CanDeviceList<FakeDevice> devices;
  IS_TRUE(devices.first() == nullptr);
  IS_TRUE(devices.find(0x101U) == nullptr);
  END_IT
}

bool test_a_null_device_is_rejected() {
  IT("appending a null device is rejected and leaves the list empty");
  CanDeviceList<FakeDevice> devices;
  IS_FALSE(devices.append(nullptr));
  IS_TRUE(devices.first() == nullptr);
  END_IT
}

bool test_a_registered_device_is_found_by_its_id() {
  IT("a registered device is found by its own client id and by no other");
  CanDeviceList<FakeDevice> devices;
  FakeDevice device(0x101U);
  IS_TRUE(devices.append(&device));
  IS_TRUE(devices.find(0x101U) == &device);
  IS_TRUE(devices.find(0x102U) == nullptr);       // an unknown sender routes nowhere
  END_IT
}

bool test_every_device_is_found_by_its_own_id() {
  IT("each of several registered devices is found by its own client id");
  CanDeviceList<FakeDevice> devices;
  FakeDevice first(0x101U);
  FakeDevice second(0x102U);
  FakeDevice third(0x103U);
  IS_TRUE(devices.append(&first));
  IS_TRUE(devices.append(&second));
  IS_TRUE(devices.append(&third));
  IS_TRUE(devices.find(0x101U) == &first);
  IS_TRUE(devices.find(0x102U) == &second);
  IS_TRUE(devices.find(0x103U) == &third);
  END_IT
}

bool test_devices_keep_their_registration_order() {
  IT("walking the list from the first device follows registration order");
  CanDeviceList<FakeDevice> devices;
  FakeDevice first(0x101U);
  FakeDevice second(0x102U);
  FakeDevice third(0x103U);
  IS_TRUE(devices.append(&first));
  IS_TRUE(devices.append(&second));
  IS_TRUE(devices.append(&third));
  // The startup log lists the devices by walking from first(); the order it prints is the
  // order they registered in.
  const FakeDevice* walk = devices.first();
  IS_TRUE(walk == &first);
  walk = walk->getNextDevice();
  IS_TRUE(walk == &second);
  walk = walk->getNextDevice();
  IS_TRUE(walk == &third);
  IS_TRUE(walk->getNextDevice() == nullptr);
  END_IT
}

bool test_the_first_registered_device_wins_a_shared_id() {
  IT("when two devices share a client id the first registered one receives the frames");
  CanDeviceList<FakeDevice> devices;
  FakeDevice first(0x101U);
  FakeDevice second(0x101U);
  IS_TRUE(devices.append(&first));
  IS_TRUE(devices.append(&second));
  IS_TRUE(devices.find(0x101U) == &first);
  END_IT
}

int main() {
  SUITE("CanDeviceList");
  test_an_empty_list_routes_nowhere();
  test_a_null_device_is_rejected();
  test_a_registered_device_is_found_by_its_id();
  test_every_device_is_found_by_its_own_id();
  test_devices_keep_their_registration_order();
  test_the_first_registered_device_wins_a_shared_id();
  FINISH
}
