#include "taskHandler.hpp"
#include "BDDTest.h"

struct SimpleTask : public Task {
  const bool result;
  explicit SimpleTask(bool r) : result(r) {}
  [[nodiscard]] bool init() override { return result; }
  [[nodiscard]] bool run()  override { return result; }
};

class TrackingTask : public Task {
public:
  const bool initResult;
  const bool runResult;
  uint16_t initCount = 0;
  uint16_t runCount = 0;

  TrackingTask(bool initOk, bool runOk) : initResult(initOk), runResult(runOk) {}

  [[nodiscard]] bool init() override { initCount++; return initResult; }
  [[nodiscard]] bool run()  override { runCount++;  return runResult;  }
};

bool test_single_task_init_success() {
  IT("single task - successful init returns 0");
  TrackingTask task(true, true);
  Task* list[] = { &task };
  TaskHandler<1, true> handler(list);

  IS_EQUAL(handler.initTasks(), 0U);
  IS_EQUAL(task.initCount, 1U);
  END_IT
}

bool test_single_task_init_failure() {
  IT("single task - failed init returns bitmask 1");
  TrackingTask task(false, true);
  Task* list[] = { &task };
  TaskHandler<1, true> handler(list);

  IS_EQUAL(handler.initTasks(), 1U);
  END_IT
}

bool test_multi_task_init_all_success() {
  IT("all tasks init successfully - returns 0");
  TrackingTask t0(true, true);
  TrackingTask t1(true, true);
  TrackingTask t2(true, true);
  Task* list[] = { &t0, &t1, &t2 };
  TaskHandler<3, true> handler(list);

  IS_EQUAL(handler.initTasks(), 0U);
  IS_EQUAL(t0.initCount, 1U);
  IS_EQUAL(t1.initCount, 1U);
  IS_EQUAL(t2.initCount, 1U);
  END_IT
}

bool test_multi_task_init_partial_failure_bitmask() {
  IT("failed init tasks set their corresponding bitmask bits");
  TrackingTask t0(true, true);
  TrackingTask t1(false, true);
  TrackingTask t2(true, true);
  TrackingTask t3(false, true);
  Task* list[] = { &t0, &t1, &t2, &t3 };
  TaskHandler<4, true> handler(list);

  IS_EQUAL(handler.initTasks(), 0b1010U); // bit 1 and bit 3
  END_IT
}

bool test_single_task_run_count() {
  IT("single task - run is called once per runTasks()");
  TrackingTask task(true, true);
  Task* list[] = { &task };
  TaskHandler<1, true> handler(list);

  IS_EQUAL(handler.runTasks(), 0U);
  IS_EQUAL(handler.runTasks(), 0U);
  IS_EQUAL(task.runCount, 2U);
  END_IT
}

bool test_full_round_robin_order() {
  IT("full round-robin cycles through tasks one at a time in order");
  TrackingTask t0(true, true);
  TrackingTask t1(true, true);
  TrackingTask t2(true, true);
  Task* list[] = { &t0, &t1, &t2 };
  TaskHandler<3, true> handler(list);

  // 4 calls: t0, t1, t2, t0
  IS_EQUAL(handler.runTasks(), 0U);
  IS_EQUAL(handler.runTasks(), 0U);
  IS_EQUAL(handler.runTasks(), 0U);
  IS_EQUAL(handler.runTasks(), 0U);

  IS_EQUAL(t0.runCount, 2U);
  IS_EQUAL(t1.runCount, 1U);
  IS_EQUAL(t2.runCount, 1U);
  END_IT
}

bool test_partial_round_robin_first_task_always_runs() {
  IT("partial round-robin always runs first task and alternates the rest");
  TrackingTask t0(true, true);
  TrackingTask t1(true, true);
  TrackingTask t2(true, true);
  Task* list[] = { &t0, &t1, &t2 };
  TaskHandler<3, false> handler(list);

  // Pattern over 6 calls: (t0,t1), (t0,t2), (t0,t1), (t0,t2), (t0,t1), (t0,t2)
  for (uint8_t i = 0U; i < 6U; i++) {
    IS_EQUAL(handler.runTasks(), 0U);
  }

  IS_EQUAL(t0.runCount, 6U);
  IS_EQUAL(t1.runCount, 3U);
  IS_EQUAL(t2.runCount, 3U);
  END_IT
}

bool test_nullptr_task_skipped() {
  IT("nullptr task entries are safely skipped in init and run");
  TrackingTask t0(true, true);
  TrackingTask t2(true, true);
  Task* list[] = { &t0, nullptr, &t2 };
  TaskHandler<3, true> handler(list);

  IS_EQUAL(handler.initTasks(), 0U); // nullptr skipped, not counted as failure
  IS_EQUAL(handler.runTasks(), 0U); // t0
  IS_EQUAL(handler.runTasks(), 0U); // nullptr - skipped without crash
  IS_EQUAL(handler.runTasks(), 0U); // t2

  IS_EQUAL(t0.runCount, 1U);
  IS_EQUAL(t2.runCount, 1U);
  END_IT
}

bool test_run_failure_sets_bitmask() {
  IT("failed run sets the task's bit in the returned bitmask");
  TrackingTask t0(true, true);
  TrackingTask t1(true, false);
  TrackingTask t2(true, true);
  Task* list[] = { &t0, &t1, &t2 };
  TaskHandler<3, true> handler(list);

  uint32_t result0 = handler.runTasks(); // runs t0 - succeeds
  uint32_t result1 = handler.runTasks(); // runs t1 - fails
  uint32_t result2 = handler.runTasks(); // runs t2 - succeeds

  IS_EQUAL(result0, 0U);
  IS_EQUAL(result1, 0b010U); // bit 1
  IS_EQUAL(result2, 0U);
  END_IT
}

bool test_32_tasks_highest_bit_bitmask() {
  IT("32 tasks: failure at index 31 sets bit 31 in the bitmask");
  SimpleTask t00(true);  SimpleTask t01(true);  SimpleTask t02(true);  SimpleTask t03(true);
  SimpleTask t04(true);  SimpleTask t05(true);  SimpleTask t06(true);  SimpleTask t07(true);
  SimpleTask t08(true);  SimpleTask t09(true);  SimpleTask t10(true);  SimpleTask t11(true);
  SimpleTask t12(true);  SimpleTask t13(true);  SimpleTask t14(true);  SimpleTask t15(true);
  SimpleTask t16(true);  SimpleTask t17(true);  SimpleTask t18(true);  SimpleTask t19(true);
  SimpleTask t20(true);  SimpleTask t21(true);  SimpleTask t22(true);  SimpleTask t23(true);
  SimpleTask t24(true);  SimpleTask t25(true);  SimpleTask t26(true);  SimpleTask t27(true);
  SimpleTask t28(true);  SimpleTask t29(true);  SimpleTask t30(true);  SimpleTask t31(false);
  Task* list[] = {
    &t00, &t01, &t02, &t03, &t04, &t05, &t06, &t07,
    &t08, &t09, &t10, &t11, &t12, &t13, &t14, &t15,
    &t16, &t17, &t18, &t19, &t20, &t21, &t22, &t23,
    &t24, &t25, &t26, &t27, &t28, &t29, &t30, &t31
  };
  TaskHandler<32U, true> handler(list);
  IS_EQUAL(handler.initTasks(), 0x80000000U);
  END_IT
}

int main() {
  SUITE("TaskHandler");
  test_single_task_init_success();
  test_single_task_init_failure();
  test_multi_task_init_all_success();
  test_multi_task_init_partial_failure_bitmask();
  test_single_task_run_count();
  test_full_round_robin_order();
  test_partial_round_robin_first_task_always_runs();
  test_nullptr_task_skipped();
  test_run_failure_sets_bitmask();
  test_32_tasks_highest_bit_bitmask();
  FINISH
}
