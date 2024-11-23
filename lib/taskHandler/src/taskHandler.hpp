#ifndef TASK_HANDLER_HPP
#define TASK_HANDLER_HPP

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
  /// @details Called once during setup to prepare the task for execution. Must be implemented by derived classes.
  /// @return `true` if the execution was successfully, `false` otherwise.
  virtual bool init() = 0;

  /// @brief Executes the task logic.
  /// @details Called repeatedly during the program loop to perform the task's operations. Must be implemented by derived classes.
  virtual void run() = 0;

  Task(const Task&) = delete;                       // Define copy constructor.
  Task& operator=(const Task&) = delete;            // Define copy assignment operator.
  Task(Task&&) = delete;                            // Define move constructor.
  Task& operator=(Task&&) = delete;                 // Define move assignment operator.
};

/// @brief Task handler for managing a collection of tasks.
/// @details Handles initialization and round-robin execution of tasks.
/// @tparam taskNum Number of tasks to manage (must be greater than 0 and less than or equal to 32).
/// @tparam fullRoundRobin If true, uses full round-robin scheduling. If false, prioritizes the first task and alternates with the others.
template<uint8_t taskNum, bool fullRoundRobin>
class TaskHandler final {
public:
  static_assert(taskNum > 0U, "TaskHandler requires at least one task!");
  static_assert(taskNum <= 32U, "TaskHandler maximum task handling limit exceeded!");

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
  uint32_t initTasks() {
    uint32_t failureMask = 0U;
    for(uint8_t i = 0U; i < taskNum; ++i) {
      if(!taskList[i]->init()) {
        failureMask |= (1U << i);
      }
    }
    return failureMask;
  }

  /// @brief Executes the tasks in a round-robin manner.
  /// @details Alternates execution based on the `fullRoundRobin` parameter:
  /// - Full round-robin (`true`): Executes tasks sequentially, one at a time.
  /// - Partial round-robin (`false`): Always runs the first task, alternating with others.
  void runTasks() {
    if(fullRoundRobin) {
      taskList[currentTask]->run();
      currentTask = (currentTask + 1U) % taskNum;
    } else {
      taskList[0]->run();
      currentTask = (currentTask % (taskNum - 1U)) + 1U;
      taskList[currentTask]->run();
    }
  }

  TaskHandler(const TaskHandler&) = delete;                       // Define copy constructor.
  TaskHandler& operator=(const TaskHandler&) = delete;            // Define copy assignment operator.
  TaskHandler(TaskHandler&&) = delete;                            // Define move constructor.
  TaskHandler& operator=(TaskHandler&&) = delete;                 // Define move assignment operator.

private:
  Task *(&taskList)[taskNum];                                     // Reference to an array of task pointers.
  uint8_t currentTask;                                            // Index of the currently executing task.
};
#endif // TASK_HANDLER_HPP