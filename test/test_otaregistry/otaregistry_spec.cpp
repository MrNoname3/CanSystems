#include "otaRegistry.hpp"
#include "BDDTest.h"
#include <string.h>

// OtaRegistry uses a static linked list that accumulates across tests.
// All OtaTarget objects are at file scope to guarantee lifetime.
// Each target has a unique file name to avoid cross-test interference.

class TestTarget : public OtaTarget {
public:
  const char* const fileName;
  bool triggered = false;
  uint8_t triggerCount = 0U;

  explicit TestTarget(const char* fn) : fileName(fn) {}
  [[nodiscard]] const char* getFwFileName() const override { return fileName; }
  void triggerOta() override { triggered = true; triggerCount++; }
};

TestTarget targetA{"fw_a.bin"};
TestTarget targetB{"fw_b.bin"};
TestTarget targetC{"fw_a.bin"}; // same file name as targetA
TestTarget targetNull{nullptr};  // null file name
TestTarget targetDup{"fw_dup.bin"};

bool test_matching_target_triggered() {
  IT("triggers the registered target whose file name matches");
  OtaRegistry::add(targetA);

  OtaRegistry::triggerForFile("fw_a.bin");
  IS_TRUE(targetA.triggered);
  targetA.triggered = false;
  END_IT
}

bool test_non_matching_target_not_triggered() {
  IT("does not trigger a target whose file name does not match");
  OtaRegistry::add(targetB);

  OtaRegistry::triggerForFile("fw_b.bin");
  IS_TRUE(targetB.triggered);
  IS_FALSE(targetA.triggered);
  targetB.triggered = false;
  END_IT
}

bool test_multiple_targets_same_file_all_triggered() {
  IT("triggers all targets that share the same file name");
  OtaRegistry::add(targetC); // same as targetA: "fw_a.bin"

  OtaRegistry::triggerForFile("fw_a.bin");
  IS_TRUE(targetA.triggered);
  IS_FALSE(targetB.triggered);
  IS_TRUE(targetC.triggered);
  targetA.triggered = false;
  targetC.triggered = false;
  END_IT
}

bool test_null_filename_trigger_is_noop() {
  IT("triggerForFile(nullptr) does not trigger any target");
  OtaRegistry::add(targetNull); // null-named target also registered

  OtaRegistry::triggerForFile(nullptr);
  IS_FALSE(targetA.triggered);
  IS_FALSE(targetB.triggered);
  IS_FALSE(targetC.triggered);
  IS_FALSE(targetNull.triggered);
  END_IT
}

bool test_unknown_file_nothing_triggered() {
  IT("no target is triggered for an unregistered file name");
  OtaRegistry::triggerForFile("unknown.bin");
  IS_FALSE(targetA.triggered);
  IS_FALSE(targetB.triggered);
  IS_FALSE(targetC.triggered);
  IS_FALSE(targetNull.triggered);
  END_IT
}

bool test_null_named_target_skipped_on_real_file() {
  IT("null-named target is never triggered even for matching non-null request");
  OtaRegistry::triggerForFile("fw_b.bin");
  IS_FALSE(targetNull.triggered);
  IS_TRUE(targetB.triggered);
  targetB.triggered = false;
  END_IT
}

bool test_duplicate_add_is_idempotent() {
  IT("adding the same target twice does not duplicate it in the registry");
  OtaRegistry::add(targetDup);
  OtaRegistry::add(targetDup); // second add must be a no-op

  targetDup.triggered = false;
  targetDup.triggerCount = 0U;
  OtaRegistry::triggerForFile("fw_dup.bin");
  IS_TRUE(targetDup.triggered);
  IS_EQUAL(targetDup.triggerCount, 1U); // triggered exactly once, not twice
  END_IT
}

int main() {
  SUITE("OtaRegistry");
  test_matching_target_triggered();
  test_non_matching_target_not_triggered();
  test_multiple_targets_same_file_all_triggered();
  test_null_filename_trigger_is_noop();
  test_unknown_file_nothing_triggered();
  test_null_named_target_skipped_on_real_file();
  test_duplicate_add_is_idempotent();
  FINISH
}
