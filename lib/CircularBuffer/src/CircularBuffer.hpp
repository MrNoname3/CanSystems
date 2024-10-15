#ifndef CIRCULAR_BUFFER_HPP
#define CIRCULAR_BUFFER_HPP

#include <stdint.h>

template<class T, uint16_t buffSize> 
class CircularBuffer final {
public:
  /// @brief Constructor of a multitype static size circular buffer.
  /// @param T Selected datatype.
  /// @param buffSize Selected buffer size.
  CircularBuffer();

  /// @brief Destructor of the object.
  virtual ~CircularBuffer() = default;

  /// @brief Puts an element to the data buffer.
  /// @param item Element to store.
  void put(T item);

  /// @brief Gets an element from the buffer.
  /// @return Returns with the element.
  T pop();

  /// @brief Gets an element from the buffer without removing it.
  /// @return Returns with the latest element.
  T peek() const;

  /// @brief Resets the buffer to its initial state.
  void clear();

  /// @brief Checks if the buffer is empty.
  /// @return Returns true if the buffer is empty.
  bool isEmpty() const;

  /// @brief Checks if the buffer is full.
  /// @return True, if the buffer is full
  bool isFull() const;

  /// @brief Checks buffer free capacity.
  /// @return Returns with the free capacity.
  uint16_t getCapacity() const;

  /// @brief Checks buffer size.
  /// @return Returns with the size.
  uint16_t getSize() const;

  CircularBuffer(const CircularBuffer&) = delete;               // Define copy constructor.
  CircularBuffer& operator=(const CircularBuffer&) = delete;    // Define copy assignment operator.
  CircularBuffer(CircularBuffer&&) = delete;                    // Define move constructor.
  CircularBuffer& operator=(CircularBuffer&&) = delete;         // Define move assignment operator.

private:
  T buffer[buffSize];                         // Data buffer.
  uint16_t head;                              // Circular buffer write index.
  uint16_t tail;                              // Circular buffer read index.
  bool full;                                  // Stores if the buffer full is.
};

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
T CircularBuffer<T, buffSize>::peek() const {
  // Handle empty buffer case, with return a default value.
  if(isEmpty()) { return T(); }
  // If the buffer is not empty, return the element at the tail index.
  return buffer[tail];
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
#endif // CIRCULAR_BUFFER_HPP