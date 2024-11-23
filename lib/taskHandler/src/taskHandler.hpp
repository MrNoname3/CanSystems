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
#endif // TASK_HANDLER_HPP