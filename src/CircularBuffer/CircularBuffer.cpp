#include "CircularBuffer.hpp"

template<class T, uint16_t buffSize>
CircularBuffer<T, buffSize>::CircularBuffer() : buffer(), head(0), tail(0), full(false) {}

template<class T, uint16_t buffSize>
void CircularBuffer<T, buffSize>::put(T item) {
  buffer[head] = item;
  head = (head + 1) % buffSize;
  if(full) { tail = (tail + 1) % buffSize; }
  full = (head == tail);
}

template<class T, uint16_t buffSize>
T CircularBuffer<T, buffSize>::pop() {
  auto result = buffer[tail];
  tail = (tail + 1) % buffSize;
  full = false;
  return result;
}

template<class T, uint16_t buffSize>
void CircularBuffer<T, buffSize>::clear() {
  head = 0;
  tail = 0;
  full = false;
}

template<class T, uint16_t buffSize>
bool CircularBuffer<T, buffSize>::isEmpty() const {
  if(full) { return false; }
  return tail == head;
}

template<class T, uint16_t buffSize>
bool CircularBuffer<T, buffSize>::isFull() const {
  return full;
}

template<class T, uint16_t buffSize>
uint16_t CircularBuffer<T, buffSize>::getCapacity() const {
  return buffSize;
}

template<class T, uint16_t buffSize>
uint16_t CircularBuffer<T, buffSize>::getSize() const {
  if(full) { return buffSize; }
  if(head >= tail) { return head - tail; }
  return buffSize + head - tail;
}