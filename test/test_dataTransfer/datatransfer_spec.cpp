#include "dataTransfer.hpp"
#include "Arduino.h"
#include "LittleFS.h"
#include "Update.h"
#include "MD5Builder.h"
#include "base64.hpp"
#include "BDDTest.h"
#include <string>
#include <cstring>

// DataTransferError bit positions (mirror of the private enum).
namespace Err {
  constexpr uint32_t FILE_SIZE_ZERO     = 1UL << 0U;
  constexpr uint32_t MD5_NULLPTR        = 1UL << 1U;
  constexpr uint32_t MD5_INVALID        = 1UL << 2U;
  constexpr uint32_t NAME_NULLPTR       = 1UL << 3U;
  constexpr uint32_t NAME_INVALID       = 1UL << 4U;
  constexpr uint32_t NAME_NOT_ALLOWED   = 1UL << 5U;
  constexpr uint32_t NOT_ENOUGH_STORAGE = 1UL << 6U;
  constexpr uint32_t BEGIN_NOT_CALLED   = 1UL << 7U;
  constexpr uint32_t WRONG_PIECE_NUMBER = 1UL << 8U;
  constexpr uint32_t DATA_NULLPTR       = 1UL << 10U;
  constexpr uint32_t B64_SIZE_ERROR     = 1UL << 11U;
  constexpr uint32_t FILE_MD5_ERROR     = 1UL << 18U;
  constexpr uint32_t FW_BEGIN_FAILED    = 1UL << 20U;
  constexpr uint32_t FW_WRITE_FAILED    = 1UL << 22U;
  constexpr uint32_t FW_END_FAILED      = 1UL << 23U;
}

static const char* kMd5 = "0123456789abcdef0123456789abcdef";

// ---- checkOk callback capture ----
static int  g_cbCount;
static bool g_lastValid;
static void onCheckOk(bool valid) { g_lastValid = valid; ++g_cbCount; }

static void resetEnv() {
  LittleFS.reset();
  Update.reset();
  md5shim::forcedResult.clear();
  g_cbCount = 0;
  g_lastValid = false;
}

static std::string b64(const std::string& raw) {
  uint8_t out[512] = {0U};
  const uint32_t n = Base64::encodeBase64(reinterpret_cast<const uint8_t*>(raw.data()), out,
                                          static_cast<uint32_t>(raw.size()), sizeof(out));
  return std::string(reinterpret_cast<char*>(out), n);
}

static const char* fileName()  { return FileName::getCanAlertFwLocation(); }   // "/canAlertFw.bin"
static const char* fwName()    { return FileName::getOtaFwLocation(); }        // "espFirmware"
static const char* tempName()  { return FileName::getTempFileLocation(); }     // "/temp.tmp"

// ---- begin() validation ----

bool test_begin_rejects_zero_size() {
  IT("begin() rejects a zero file size");
  resetEnv();
  DataTransfer dt(onCheckOk);
  IS_FALSE(dt.begin(0U, kMd5, fileName()));
  IS_EQUAL(dt.getErrorCode(), Err::FILE_SIZE_ZERO);
  END_IT
}

bool test_begin_rejects_null_md5() {
  IT("begin() rejects a null MD5");
  resetEnv();
  DataTransfer dt(onCheckOk);
  IS_FALSE(dt.begin(10U, nullptr, fileName()));
  IS_EQUAL(dt.getErrorCode(), Err::MD5_NULLPTR);
  END_IT
}

bool test_begin_rejects_empty_md5() {
  IT("begin() rejects an empty MD5");
  resetEnv();
  DataTransfer dt(onCheckOk);
  IS_FALSE(dt.begin(10U, "", fileName()));
  IS_EQUAL(dt.getErrorCode(), Err::MD5_INVALID);
  END_IT
}

bool test_begin_rejects_null_name() {
  IT("begin() rejects a null file name");
  resetEnv();
  DataTransfer dt(onCheckOk);
  IS_FALSE(dt.begin(10U, kMd5, nullptr));
  IS_EQUAL(dt.getErrorCode(), Err::NAME_NULLPTR);
  END_IT
}

bool test_begin_rejects_empty_name() {
  IT("begin() rejects an empty file name");
  resetEnv();
  DataTransfer dt(onCheckOk);
  IS_FALSE(dt.begin(10U, kMd5, ""));
  IS_EQUAL(dt.getErrorCode(), Err::NAME_INVALID);
  END_IT
}

bool test_begin_rejects_disallowed_name() {
  IT("begin() rejects a file name not on the allow-list");
  resetEnv();
  DataTransfer dt(onCheckOk);
  IS_FALSE(dt.begin(10U, kMd5, "/etc/passwd"));
  IS_EQUAL(dt.getErrorCode(), Err::NAME_NOT_ALLOWED);
  END_IT
}

bool test_begin_rejects_insufficient_storage() {
  IT("begin() rejects a file larger than the free space");
  resetEnv();
  LittleFS.setCapacity(8U);                   // tiny FS
  DataTransfer dt(onCheckOk);
  IS_FALSE(dt.begin(100U, kMd5, fileName())); // 100 > 8
  IS_EQUAL(dt.getErrorCode(), Err::NOT_ENOUGH_STORAGE);
  END_IT
}

bool test_begin_succeeds_for_valid_file() {
  IT("begin() succeeds for a valid file and exposes the name");
  resetEnv();
  DataTransfer dt(onCheckOk);
  IS_TRUE(dt.begin(10U, kMd5, fileName()));
  IS_EQUAL(dt.getErrorCode(), 0U);
  IS_TRUE(strcmp(dt.getFileName(), fileName()) == 0);
  END_IT
}

// ---- storeBase64() validation ----

bool test_store_before_begin_fails() {
  IT("storeBase64() fails when begin() was not called");
  resetEnv();
  DataTransfer dt(onCheckOk);
  IS_FALSE(dt.storeBase64(0U, b64("data").c_str()));
  IS_EQUAL(dt.getErrorCode(), Err::BEGIN_NOT_CALLED);
  END_IT
}

bool test_store_wrong_piece_number_fails() {
  IT("storeBase64() rejects an out-of-sequence piece number");
  resetEnv();
  DataTransfer dt(onCheckOk);
  IS_TRUE(dt.begin(10U, kMd5, fileName()));
  IS_FALSE(dt.storeBase64(5U, b64("data").c_str()));
  IS_EQUAL(dt.getErrorCode(), Err::WRONG_PIECE_NUMBER);
  END_IT
}

bool test_store_null_data_fails() {
  IT("storeBase64() rejects null data");
  resetEnv();
  DataTransfer dt(onCheckOk);
  IS_TRUE(dt.begin(10U, kMd5, fileName()));
  IS_FALSE(dt.storeBase64(0U, nullptr));
  IS_EQUAL(dt.getErrorCode(), Err::DATA_NULLPTR);
  END_IT
}

bool test_store_unaligned_base64_fails() {
  IT("storeBase64() rejects base64 whose length is not a multiple of 4");
  resetEnv();
  DataTransfer dt(onCheckOk);
  IS_TRUE(dt.begin(10U, kMd5, fileName()));
  IS_FALSE(dt.storeBase64(0U, "abc"));        // length 3
  IS_EQUAL(dt.getErrorCode(), Err::B64_SIZE_ERROR);
  END_IT
}

// ---- happy path (file transfer) ----

bool test_full_file_transfer_succeeds() {
  IT("a full file transfer writes, verifies MD5 and renames the temp file into place");
  resetEnv();
  const std::string raw = "Hello DataTransfer!";   // 19 bytes, one piece
  md5shim::setResult(kMd5);                         // MD5 check will match
  DataTransfer dt(onCheckOk);
  IS_TRUE(dt.begin(static_cast<uint32_t>(raw.size()), kMd5, fileName()));
  IS_TRUE(dt.storeBase64(0U, b64(raw).c_str()));    // completes -> CHECK state
  dt.runValidityCheck();                            // read + hash
  dt.runValidityCheck();                            // compare + rename
  IS_EQUAL(g_cbCount, 1);
  IS_TRUE(g_lastValid);
  IS_TRUE(LittleFS.exists(fileName()));
  IS_TRUE(LittleFS.fileContent(fileName()) == raw);
  IS_FALSE(LittleFS.exists(tempName()));            // temp renamed away
  END_IT
}

bool test_multi_piece_transfer_succeeds() {
  IT("a transfer split across two pieces reassembles the file in order");
  resetEnv();
  const std::string part0 = "abc";                  // 3 bytes -> 4 b64 chars
  const std::string part1 = "def";
  md5shim::setResult(kMd5);
  DataTransfer dt(onCheckOk);
  IS_TRUE(dt.begin(6U, kMd5, fileName()));
  IS_TRUE(dt.storeBase64(0U, b64(part0).c_str()));
  IS_TRUE(dt.storeBase64(1U, b64(part1).c_str()));  // completes
  dt.runValidityCheck();
  dt.runValidityCheck();
  IS_TRUE(g_lastValid);
  IS_TRUE(LittleFS.fileContent(fileName()) == "abcdef");
  END_IT
}

bool test_md5_mismatch_fails_and_keeps_no_file() {
  IT("an MD5 mismatch reports FILE_MD5_ERROR and does not publish the file");
  resetEnv();
  const std::string raw = "payload-bytes";
  md5shim::setResult("ffffffffffffffffffffffffffffffff");  // != kMd5
  DataTransfer dt(onCheckOk);
  IS_TRUE(dt.begin(static_cast<uint32_t>(raw.size()), kMd5, fileName()));
  IS_TRUE(dt.storeBase64(0U, b64(raw).c_str()));
  dt.runValidityCheck();                            // read + hash
  dt.runValidityCheck();                            // compare -> mismatch -> CLEANUP
  dt.runValidityCheck();                            // CLEANUP -> callback(false)
  IS_EQUAL(dt.getErrorCode(), Err::FILE_MD5_ERROR);
  IS_TRUE(g_lastValid == false);
  IS_TRUE(g_cbCount >= 1);
  IS_FALSE(LittleFS.exists(fileName()));
  END_IT
}

// ---- firmware (Update) path ----

bool test_firmware_transfer_succeeds() {
  IT("a firmware transfer streams to Update and finishes on the last piece");
  resetEnv();
  const std::string raw = "FIRMWAREBLOB";
  DataTransfer dt(onCheckOk);
  IS_TRUE(dt.begin(static_cast<uint32_t>(raw.size()), kMd5, fwName()));
  IS_TRUE(dt.storeBase64(0U, b64(raw).c_str()));    // completes via Update.end()
  IS_EQUAL(g_cbCount, 1);
  IS_TRUE(g_lastValid);
  IS_EQUAL(Update.written(), raw.size());
  END_IT
}

bool test_firmware_begin_failure() {
  IT("begin() reports FW_UPGRADE_BEGIN_FAILED when Update.begin fails");
  resetEnv();
  Update.setBeginResult(false);
  DataTransfer dt(onCheckOk);
  IS_FALSE(dt.begin(16U, kMd5, fwName()));
  IS_EQUAL(dt.getErrorCode(), Err::FW_BEGIN_FAILED);
  END_IT
}

bool test_firmware_write_failure() {
  IT("storeBase64() reports FW_UPGRADE_WRITE_FAILED when Update.write fails");
  resetEnv();
  Update.setWriteSucceeds(false);
  const std::string raw = "FIRMWAREBLOB";
  DataTransfer dt(onCheckOk);
  IS_TRUE(dt.begin(static_cast<uint32_t>(raw.size()), kMd5, fwName()));
  IS_FALSE(dt.storeBase64(0U, b64(raw).c_str()));
  IS_EQUAL(dt.getErrorCode(), Err::FW_WRITE_FAILED);
  END_IT
}

bool test_firmware_end_failure() {
  IT("a firmware transfer reports FW_UPGRADE_END_FAILED when Update.end fails");
  resetEnv();
  Update.setEndResult(false);
  const std::string raw = "FIRMWAREBLOB";
  DataTransfer dt(onCheckOk);
  IS_TRUE(dt.begin(static_cast<uint32_t>(raw.size()), kMd5, fwName()));
  IS_FALSE(dt.storeBase64(0U, b64(raw).c_str()));   // completes -> end() fails
  IS_EQUAL(dt.getErrorCode(), Err::FW_END_FAILED);
  END_IT
}

int main() {
  SUITE("DataTransfer");
  test_begin_rejects_zero_size();
  test_begin_rejects_null_md5();
  test_begin_rejects_empty_md5();
  test_begin_rejects_null_name();
  test_begin_rejects_empty_name();
  test_begin_rejects_disallowed_name();
  test_begin_rejects_insufficient_storage();
  test_begin_succeeds_for_valid_file();
  test_store_before_begin_fails();
  test_store_wrong_piece_number_fails();
  test_store_null_data_fails();
  test_store_unaligned_base64_fails();
  test_full_file_transfer_succeeds();
  test_multi_piece_transfer_succeeds();
  test_md5_mismatch_fails_and_keeps_no_file();
  test_firmware_transfer_succeeds();
  test_firmware_begin_failure();
  test_firmware_write_failure();
  test_firmware_end_failure();
  FINISH
}
