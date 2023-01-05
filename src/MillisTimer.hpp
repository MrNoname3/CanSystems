#ifndef __MILLIS_TIMER_HPP__
#define __MILLIS_TIMER_HPP__

#include <Arduino.h>

class MillisTimer {

public:

  MillisTimer();

  virtual ~MillisTimer();

  MillisTimer(const uint32_t interval);

  bool startTimer(const uint32_t interval);

  bool startTimer(void);

  bool readTimer(void);

  void reloadTimer(void);

private:

  uint32_t interval = 0;
  uint32_t timer = 0;

};

#endif