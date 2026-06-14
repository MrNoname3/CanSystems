#include "eepromHandler.hpp"
#include "EEPROM.h"
#include "BDDTest.h"

// Simple struct used as stored payload in most tests.
struct Config {
  uint8_t x;
  uint16_t y;
};

// ---- save() null pointer ----

bool test_static_save_null_returns_false() {
  IT("static save(nullptr) returns false without touching EEPROM");
  IS_FALSE((EEPROMHandler<uint8_t, 0U>::save(nullptr)));
  END_IT
}

// ---- load() null pointer ----

bool test_static_load_null_returns_false() {
  IT("static load(nullptr) returns false without touching EEPROM");
  IS_FALSE((EEPROMHandler<uint8_t, 0U>::load(nullptr)));
  END_IT
}

// ---- save / load round-trip ----

bool test_save_load_uint8_roundtrip() {
  IT("save then load restores a uint8_t value exactly");
  EEPROM.clear();
  uint8_t saved = 0x42U;
  uint8_t loaded = 0U;
  IS_TRUE((EEPROMHandler<uint8_t, 10U>::save(&saved)));
  IS_TRUE((EEPROMHandler<uint8_t, 10U>::load(&loaded)));
  IS_EQUAL(loaded, 0x42U);
  END_IT
}

bool test_save_load_struct_roundtrip() {
  IT("save then load restores a struct value field by field");
  EEPROM.clear();
  Config saved{ 0xABU, 0x1234U };
  Config loaded{};
  IS_TRUE((EEPROMHandler<Config, 20U>::save(&saved)));
  IS_TRUE((EEPROMHandler<Config, 20U>::load(&loaded)));
  IS_EQUAL(loaded.x, 0xABU);
  IS_EQUAL(loaded.y, 0x1234U);
  END_IT
}

// ---- fresh EEPROM (all 0xFF) ----

bool test_load_fresh_eeprom_returns_false() {
  IT("load returns false on a fresh (all 0xFF) EEPROM due to CRC mismatch");
  EEPROM.clear();
  uint8_t loaded = 0U;
  IS_FALSE((EEPROMHandler<uint8_t, 30U>::load(&loaded)));
  END_IT
}

// ---- getDataSize ----

bool test_get_data_size_uint8() {
  IT("getDataSize returns sizeof(uint16_t crc) + sizeof(uint8_t) = 3");
  IS_EQUAL((EEPROMHandler<uint8_t, 0U>::getDataSize()),
           static_cast<uint16_t>(sizeof(uint16_t) + sizeof(uint8_t)));
  END_IT
}

bool test_get_data_size_uint32() {
  IT("getDataSize returns sizeof(uint16_t crc) + sizeof(uint32_t) = 6");
  IS_EQUAL((EEPROMHandler<uint32_t, 0U>::getDataSize()),
           static_cast<uint16_t>(sizeof(uint16_t) + sizeof(uint32_t)));
  END_IT
}

// ---- two independent addresses ----

bool test_two_addresses_do_not_interfere() {
  IT("data saved at two different EEPROM addresses is loaded independently");
  EEPROM.clear();
  uint16_t val0 = 0xAAAAU;
  uint16_t val1 = 0xBBBBU;
  IS_TRUE((EEPROMHandler<uint16_t, 0U>::save(&val0)));
  IS_TRUE((EEPROMHandler<uint16_t, 50U>::save(&val1)));

  uint16_t out0 = 0U;
  uint16_t out1 = 0U;
  IS_TRUE((EEPROMHandler<uint16_t, 0U>::load(&out0)));
  IS_TRUE((EEPROMHandler<uint16_t, 50U>::load(&out1)));
  IS_EQUAL(out0, 0xAAAAU);
  IS_EQUAL(out1, 0xBBBBU);
  END_IT
}

// ---- corrupt data → CRC mismatch ----

bool test_load_fails_on_corrupt_data() {
  IT("load returns false when the data byte is corrupted after save");
  EEPROM.clear();
  uint8_t val = 0x55U;
  IS_TRUE((EEPROMHandler<uint8_t, 5U>::save(&val)));
  // EEPROMData layout at addr=5: [crc:2 bytes][data:1 byte]
  // Corrupt the data byte at absolute address 7.
  const uint8_t corrupted = 0xAAU;
  EEPROM.put(static_cast<uint16_t>(7U), corrupted);
  uint8_t loaded = 0U;
  IS_FALSE((EEPROMHandler<uint8_t, 5U>::load(&loaded)));
  END_IT
}

// ---- instance pointer constructor ----

bool test_instance_save_load() {
  IT("instance save() and load() work through the pointer set in the constructor");
  EEPROM.clear();
  uint32_t data = 0xDEADBEEFU;
  EEPROMHandler<uint32_t, 80U> saver(&data);
  IS_TRUE(saver.save());

  uint32_t out = 0U;
  EEPROMHandler<uint32_t, 80U> loader(&out);
  IS_TRUE(loader.load());
  IS_EQUAL(out, 0xDEADBEEFU);
  END_IT
}

bool test_instance_null_pointer_save_returns_false() {
  IT("instance save() returns false when constructed with nullptr");
  EEPROMHandler<uint32_t, 90U> h(nullptr);
  IS_FALSE(h.save());
  END_IT
}

bool test_instance_null_pointer_load_returns_false() {
  IT("instance load() returns false when constructed with nullptr");
  EEPROMHandler<uint32_t, 90U> h(nullptr);
  IS_FALSE(h.load());
  END_IT
}

int main() {
  SUITE("EEPROMHandler");
  test_static_save_null_returns_false();
  test_static_load_null_returns_false();
  test_save_load_uint8_roundtrip();
  test_save_load_struct_roundtrip();
  test_load_fresh_eeprom_returns_false();
  test_get_data_size_uint8();
  test_get_data_size_uint32();
  test_two_addresses_do_not_interfere();
  test_load_fails_on_corrupt_data();
  test_instance_save_load();
  test_instance_null_pointer_save_returns_false();
  test_instance_null_pointer_load_returns_false();
  FINISH
}
