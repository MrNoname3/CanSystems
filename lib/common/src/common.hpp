#ifndef COMMON_HPP
#define COMMON_HPP

#include <stdint.h>

class TimeConverter {
public:
  static constexpr uint32_t hrToMs(uint16_t hour) { return hour * 60UL * 60UL * 1000UL; }
  static constexpr uint32_t minToMs(uint16_t minute) { return minute * 60UL * 1000UL; }
  static constexpr uint32_t secToMs(uint16_t second) { return second * 1000UL; }
  static constexpr uint16_t hrToMin(uint16_t hour) { return hour * 60U; }
};

#endif // COMMON_HPP