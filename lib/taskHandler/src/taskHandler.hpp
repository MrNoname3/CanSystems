#ifndef TASK_HANDLER_HPP
#define TASK_HANDLER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Abstract base class for tasks that require periodic execution.
/// @details Derive from this class to create objects that can be initialized and executed periodically.
/// Classes inheriting from `Task` must implement the `init()` and `run()` methods.
class Task {
public:
  /// @brief Default constructor.
  Task() = default;

  /// @brief Virtual destructor.
  /// @details Ensures proper cleanup of derived class objects when destroyed via a base pointer.
  virtual ~Task() = default;

  /// @brief Initializes the task.
  /// @return `true` if the execution was successfully, `false` otherwise.
  [[nodiscard]] virtual bool init() = 0;

  /// @brief Executes the task logic.
  /// @return `true` if the task executed successfully, `false` otherwise.
  [[nodiscard]] virtual bool run() = 0;

  Task(const Task&) = delete;                       // Define copy constructor.
  Task& operator=(const Task&) = delete;            // Define copy assignment operator.
  Task(Task&&) = delete;                            // Define move constructor.
  Task& operator=(Task&&) = delete;                 // Define move assignment operator.
};

/// @brief Task handler for managing a collection of tasks.
/// @details Handles initialization and round-robin execution of tasks.
/// @tparam taskNumber Number of tasks to manage (must be greater than 0 and less than or equal to 32).
/// @tparam fullRoundRobin If true, uses full round-robin scheduling. If false, prioritizes the first task and alternates with the others.
template<uint8_t taskNumber, bool fullRoundRobin>
class TaskHandler final {
private:
  static constexpr uint8_t taskNum = taskNumber;                  // The number of tasks managed by the handler.
  static constexpr bool singleTaskOnly = (taskNum == 1U);         // Indicates if the task handler has only 1 task.

  // Ensures the number of tasks is within the valid range.
  static_assert(taskNum > 0U, "TaskHandler requires at least one task!");
  static_assert(taskNum <= 32U, "TaskHandler maximum task handling limit exceeded!");

public:
  /// @brief Constructor for TaskHandler.
  /// @param taskListRef Reference to an array of pointers to `Task` objects.
  TaskHandler(Task *(&taskListRef)[taskNum]) :
    taskList(taskListRef),
    currentTask(0U)
  {}

  /// @brief Default destructor.
  ~TaskHandler() = default;

  /// @brief Initializes all tasks managed by the handler.
  /// @return A bitmask representing tasks that failed initialization. Each bit corresponds to a task index.
  [[nodiscard]] uint32_t initTasks() {
    uint32_t failureMask = 0U;
    if constexpr(singleTaskOnly) {
      if(taskList[0] != nullptr) {
        if(!taskList[0]->init()) {
          failureMask = 1U;
        }
      }
    } else {
      for(uint8_t i = 0U; i < taskNum; ++i) {
        if(taskList[i] != nullptr) {
          if(!taskList[i]->init()) {
            failureMask |= (1U << i);
          }
        }
      }
    }
    return failureMask;
  }

  /// @brief Executes the tasks in a round-robin manner.
  /// @details Alternates execution based on the `fullRoundRobin` parameter:
  /// - Full round-robin (`true`): Executes tasks sequentially, one at a time.
  /// - Partial round-robin (`false`): Always runs the first task, alternating with others.
  /// @return A bitmask representing tasks that failed execution. Each bit corresponds to a task index.
  [[nodiscard]] uint32_t runTasks() {
    if constexpr(singleTaskOnly) {
      return runSingleTask();
    } else if constexpr(fullRoundRobin) {
      return runFullRoundRobin();
    } else {
      return runPartialRoundRobin();
    }
  }

  TaskHandler(const TaskHandler&) = delete;                       // Define copy constructor.
  TaskHandler& operator=(const TaskHandler&) = delete;            // Define copy assignment operator.
  TaskHandler(TaskHandler&&) = delete;                            // Define move constructor.
  TaskHandler& operator=(TaskHandler&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Executes the single managed task.
  /// @return A bitmask with bit 0 set if the task failed, otherwise 0.
  [[nodiscard]] inline uint32_t runSingleTask() {
    if(taskList[0] != nullptr) {
      if(!taskList[0]->run()) {
        return 1U;
      }
    }
    return 0U;
  }

  /// @brief Executes the next task in full round-robin order.
  /// @return A bitmask with the corresponding bit set if the task failed, otherwise 0.
  [[nodiscard]] inline uint32_t runFullRoundRobin() {
    uint32_t failureMask = 0U;
    if(taskList[currentTask] != nullptr) {
      if(!taskList[currentTask]->run()) {
        failureMask |= (1U << currentTask);
      }
    }
    if(++currentTask >= taskNum) { currentTask = 0U; }
    return failureMask;
  }

  /// @brief Executes the first task and one additional task in partial round-robin order.
  /// @return A bitmask with the corresponding bits set for any failed tasks, otherwise 0.
  [[nodiscard]] inline uint32_t runPartialRoundRobin() {
    uint32_t failureMask = 0U;
    if(taskList[0] != nullptr) {
      if(!taskList[0]->run()) {
        failureMask |= 1U;
      }
    }
    if(++currentTask >= taskNum) { currentTask = 1U; }
    if(taskList[currentTask] != nullptr) {
      if(!taskList[currentTask]->run()) {
        failureMask |= (1U << currentTask);
      }
    }
    return failureMask;
  }

  Task *(&taskList)[taskNum];                                     // Reference to an array of task pointers.
  uint8_t currentTask;                                            // Index of the currently executing task.
};
#endif // TASK_HANDLER_HPP