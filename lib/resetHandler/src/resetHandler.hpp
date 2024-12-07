#ifndef RESET_HANDLER_HPP
#define RESET_HANDLER_HPP

/// @brief Class for handling system resets.
class ResetHandler final {
private:
  /// @brief Delete constructor.
  ResetHandler() = delete;

  /// @brief Delete destructor.
  ~ResetHandler() = delete;

public:
  /// @brief Resets the microcontroller unit (MCU) by triggering a watchdog reset.
  static void restartMCU();

  ResetHandler(const ResetHandler&) = delete;                       // Define copy constructor.
  ResetHandler& operator=(const ResetHandler&) = delete;            // Define copy assignment operator.
  ResetHandler(ResetHandler&&) = delete;                            // Define move constructor.
  ResetHandler& operator=(ResetHandler&&) = delete;                 // Define move assignment operator.
};
#endif // RESET_HANDLER_HPP