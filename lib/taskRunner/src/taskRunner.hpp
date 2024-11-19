#ifndef TASK_RUNNER_HPP
#define TASK_RUNNER_HPP

/// @brief Abstract base class for tasks that require periodic execution.
/// @details Derive from this class to create objects that can be initialized and executed periodically.
/// Classes inheriting from `TaskRunner` must implement the `init()` and `run()` methods.
class TaskRunner {
public:
  /// @brief Default constructor.
  TaskRunner() = default;

  /// @brief Virtual destructor.
  /// @details Ensures proper cleanup of derived class objects when destroyed via a base pointer.
  virtual ~TaskRunner() = default;

  /// @brief Initializes the task.
  /// @details Called once during setup to prepare the task for execution. Must be implemented by derived classes.
  virtual void init() = 0;

  /// @brief Executes the task logic.
  /// @details Called repeatedly during the program loop to perform the task's operations. Must be implemented by derived classes.
  virtual void run() = 0;

  TaskRunner(const TaskRunner&) = delete;                       // Define copy constructor.
  TaskRunner& operator=(const TaskRunner&) = delete;            // Define copy assignment operator.
  TaskRunner(TaskRunner&&) = delete;                            // Define move constructor.
  TaskRunner& operator=(TaskRunner&&) = delete;                 // Define move assignment operator.
};
#endif // TASK_RUNNER_HPP