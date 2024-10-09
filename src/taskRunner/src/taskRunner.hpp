#ifndef TASK_RUNNER_HPP
#define TASK_RUNNER_HPP

class TaskRunner {
public:
  /// @brief Constructor of the object.
  TaskRunner() = default;
  /// @brief Destructor of the object.
  ~TaskRunner() = default;

  virtual void init() = 0;
  virtual void run() = 0;

  TaskRunner(const TaskRunner&) = delete;                       // Define copy constructor.
  TaskRunner& operator=(const TaskRunner&) = delete;            // Define copy assignment operator.
  TaskRunner(TaskRunner&&) = delete;                            // Define move constructor.
  TaskRunner& operator=(TaskRunner&&) = delete;                 // Define move assignment operator.
};
#endif // TASK_RUNNER_HPP