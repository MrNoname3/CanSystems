#include "millistimer.hpp"

MillisTimer::MillisTimer() {
}

MillisTimer::~MillisTimer() {
}

MillisTimer::MillisTimer(const uint32_t interval) {
  this->interval = interval;
}

bool MillisTimer::startTimer(const uint32_t interval) {
  if(interval == 0) {
    return false;
  }
  this->interval = interval;
  reloadTimer();
  return true;
}

bool MillisTimer::startTimer(void) {
  return startTimer(interval);
}

bool MillisTimer::readTimer(void) {
  if(millis() - timer >= interval) {
    reloadTimer();
    return true;
  }
  return false;
}

void MillisTimer::reloadTimer(void) {
  timer = millis();
}