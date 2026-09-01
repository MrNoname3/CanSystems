#include "otaRegistry.hpp"
#include "BDDTest.h"
#include <string.h>

// OtaRegistry uses a static linked list that accumulates across tests, so every OtaTarget lives
// at file scope and each test leaves its targets idle - a target left mid-transfer would hold
// the queue closed for every test after it.

class TestTarget : public OtaTarget {
public:
  const char* const fileName;
  bool online = true;             // What isOtaTargetOnline() reports.
  bool startTakes = true;         // Whether triggerOta() leaves a transfer running.
  bool busy = false;              // What isOtaInProgress() reports.
  uint8_t triggerCount = 0U;
  OtaImageInfo seenImage{};       // The image facts triggerOta() was handed.
  OtaImageInfo* imageRef = nullptr;  // Where to write them back, as a gateway does after checksumming.

  explicit TestTarget(const char* fn) :
    fileName(fn) {}

  [[nodiscard]] const char* getFwFileName() const override { return fileName; }
  [[nodiscard]] bool isOtaTargetOnline() const override { return online; }
  [[nodiscard]] bool isOtaInProgress() const override { return busy; }

  void triggerOta(OtaImageInfo& image) override {
    ++triggerCount;
    seenImage = image;
    imageRef = &image;
    busy = startTakes;
  }

  /// @brief Ends the transfer and lets the queue move on.
  void finish() {
    busy = false;
    OtaRegistry::startNext();
  }

  /// @brief Ends the transfer having checksummed the image, as a gateway's START pass does.
  void finishWithChecksum(uint32_t size, uint16_t crc) {
    if(imageRef != nullptr) {
      imageRef->size = size;
      imageRef->crc = crc;
      imageRef->valid = true;
    }
    finish();
  }

  void reset() {
    online = true;
    startTakes = true;
    busy = false;
    triggerCount = 0U;
    seenImage = OtaImageInfo{};
    imageRef = nullptr;
  }
};

TestTarget targetA{ "fw_a.bin" };
TestTarget targetB{ "fw_b.bin" };
TestTarget targetC{ "fw_a.bin" };  // same file name as targetA
TestTarget targetNull{ nullptr };  // null file name
TestTarget targetDup{ "fw_dup.bin" };

static void resetAll() {
  targetA.reset();
  targetB.reset();
  targetC.reset();
  targetNull.reset();
  targetDup.reset();
}

bool test_matching_target_started() {
  IT("starts the registered target whose file name matches");
  OtaRegistry::add(targetA);
  resetAll();

  OtaRegistry::queueForFile("fw_a.bin");
  IS_EQUAL(targetA.triggerCount, 1U);
  targetA.finish();
  END_IT
}

bool test_non_matching_target_not_started() {
  IT("does not start a target whose file name does not match");
  OtaRegistry::add(targetB);
  resetAll();

  OtaRegistry::queueForFile("fw_b.bin");
  IS_EQUAL(targetB.triggerCount, 1U);
  IS_EQUAL(targetA.triggerCount, 0U);
  targetB.finish();
  END_IT
}

bool test_targets_sharing_a_file_run_one_at_a_time() {
  IT("a second target expecting the same file waits until the first has finished");
  OtaRegistry::add(targetC);  // same file as targetA
  resetAll();

  OtaRegistry::queueForFile("fw_a.bin");
  IS_EQUAL(targetA.triggerCount, 1U);
  IS_EQUAL(targetC.triggerCount, 0U);   // queued, not started
  IS_EQUAL(targetB.triggerCount, 0U);

  targetA.finish();
  IS_EQUAL(targetC.triggerCount, 1U);   // its turn came with the first one's end
  IS_TRUE(targetC.busy);
  targetC.finish();
  END_IT
}

bool test_the_checksum_is_computed_once_for_the_batch() {
  IT("the second target of an upload is handed the checksum the first one computed");
  resetAll();

  OtaRegistry::queueForFile("fw_a.bin");
  IS_FALSE(targetA.seenImage.valid);      // nothing is known about a file that just arrived
  targetA.finishWithChecksum(19018U, 0xB0CAU);

  IS_TRUE(targetC.seenImage.valid);       // so the second one does not read the file again
  IS_EQUAL(targetC.seenImage.size, 19018U);
  IS_EQUAL(targetC.seenImage.crc, 0xB0CAU);
  targetC.finish();
  END_IT
}

bool test_a_new_upload_invalidates_the_shared_checksum() {
  IT("a checksum from a previous upload is never handed to the next one");
  resetAll();

  OtaRegistry::queueForFile("fw_a.bin");
  targetA.finishWithChecksum(19018U, 0xB0CAU);
  targetC.finish();

  // Same file name, different contents: carrying the old checksum over would send a firmware
  // the node then rejects on its own CRC check.
  resetAll();
  OtaRegistry::queueForFile("fw_a.bin");
  IS_FALSE(targetA.seenImage.valid);
  targetA.finish();
  targetC.finish();
  END_IT
}

bool test_offline_target_is_skipped() {
  IT("a target that is not answering is skipped, and the next one runs instead");
  resetAll();
  targetA.online = false;

  OtaRegistry::queueForFile("fw_a.bin");
  IS_EQUAL(targetA.triggerCount, 0U);   // nothing to send to
  IS_EQUAL(targetC.triggerCount, 1U);   // the queue moved straight on
  targetC.finish();
  END_IT
}

bool test_a_start_that_does_not_take_does_not_stall_the_queue() {
  IT("a target that fails to start hands the turn on instead of holding the queue");
  resetAll();
  targetA.startTakes = false;

  OtaRegistry::queueForFile("fw_a.bin");
  IS_EQUAL(targetA.triggerCount, 1U);   // it was asked
  IS_FALSE(targetA.busy);               // but nothing is running
  IS_EQUAL(targetC.triggerCount, 1U);   // so the next one went at once
  targetC.finish();
  END_IT
}

bool test_null_filename_queue_is_noop() {
  IT("queueForFile(nullptr) starts nothing");
  OtaRegistry::add(targetNull);
  resetAll();

  OtaRegistry::queueForFile(nullptr);
  IS_EQUAL(targetA.triggerCount, 0U);
  IS_EQUAL(targetB.triggerCount, 0U);
  IS_EQUAL(targetC.triggerCount, 0U);
  IS_EQUAL(targetNull.triggerCount, 0U);
  END_IT
}

bool test_unknown_file_starts_nothing() {
  IT("no target is started for an unregistered file name");
  resetAll();
  OtaRegistry::queueForFile("unknown.bin");
  IS_EQUAL(targetA.triggerCount, 0U);
  IS_EQUAL(targetB.triggerCount, 0U);
  IS_EQUAL(targetC.triggerCount, 0U);
  IS_EQUAL(targetNull.triggerCount, 0U);
  END_IT
}

bool test_null_named_target_skipped_on_real_file() {
  IT("a null-named target is never started, even for a matching non-null request");
  resetAll();
  OtaRegistry::queueForFile("fw_b.bin");
  IS_EQUAL(targetNull.triggerCount, 0U);
  IS_EQUAL(targetB.triggerCount, 1U);
  targetB.finish();
  END_IT
}

bool test_duplicate_add_is_idempotent() {
  IT("adding the same target twice does not duplicate it in the registry");
  OtaRegistry::add(targetDup);
  OtaRegistry::add(targetDup);  // second add must be a no-op
  resetAll();

  OtaRegistry::queueForFile("fw_dup.bin");
  IS_EQUAL(targetDup.triggerCount, 1U);  // started exactly once, not twice
  targetDup.finish();
  IS_EQUAL(targetDup.triggerCount, 1U);  // and it is not in the queue a second time either
  END_IT
}

bool test_case_sensitivity() {
  IT("queueForFile is case-sensitive; the wrong case starts nothing");
  resetAll();
  OtaRegistry::queueForFile("FW_B.BIN");
  IS_EQUAL(targetB.triggerCount, 0U);
  OtaRegistry::queueForFile("Fw_b.Bin");
  IS_EQUAL(targetB.triggerCount, 0U);
  END_IT
}

bool test_a_new_upload_replaces_the_previous_queue() {
  IT("a second upload while one is queued replaces what is waiting");
  resetAll();

  OtaRegistry::queueForFile("fw_a.bin");   // targetA runs, targetC waits
  IS_EQUAL(targetA.triggerCount, 1U);
  OtaRegistry::queueForFile("fw_b.bin");   // a different file arrives
  IS_EQUAL(targetB.triggerCount, 0U);      // targetA is still transferring, so nothing starts yet

  targetA.finish();
  IS_EQUAL(targetC.triggerCount, 0U);      // the old queue is gone
  IS_EQUAL(targetB.triggerCount, 1U);      // the new one took its place
  targetB.finish();
  END_IT
}

int main() {
  SUITE("OtaRegistry");
  test_matching_target_started();
  test_non_matching_target_not_started();
  test_targets_sharing_a_file_run_one_at_a_time();
  test_the_checksum_is_computed_once_for_the_batch();
  test_a_new_upload_invalidates_the_shared_checksum();
  test_offline_target_is_skipped();
  test_a_start_that_does_not_take_does_not_stall_the_queue();
  test_null_filename_queue_is_noop();
  test_unknown_file_starts_nothing();
  test_null_named_target_skipped_on_real_file();
  test_duplicate_add_is_idempotent();
  test_case_sensitivity();
  test_a_new_upload_replaces_the_previous_queue();
  FINISH
}
