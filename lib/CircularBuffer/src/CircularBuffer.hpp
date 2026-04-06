#ifndef CIRCULAR_BUFFER_HPP
#define CIRCULAR_BUFFER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Template-based circular buffer for static-sized data storage.
/// @tparam T The type of elements stored in the buffer.
/// @tparam buffSize The size of the circular buffer.
template<class T, uint16_t buffSize>
class CircularBuffer final {
public:
  /// @brief Constructs a circular buffer with a fixed size.
  CircularBuffer();

  /// @brief Destructor for the circular buffer.
  ~CircularBuffer() = default;

  /// @brief Adds an element to the buffer.
  /// @param item The element to add to the buffer.
  /// @details Overwrites the oldest element if the buffer is full.
  void put(T item);

  /// @brief Removes and retrieves the oldest element in the buffer.
  /// @return The oldest element in the buffer.
  /// @note If the buffer is empty, this function may return a default-constructed element.
  [[nodiscard]] T pop();

  /// @brief Retrieves the oldest element in the buffer without removing it.
  /// @return The oldest element in the buffer.
  /// @note Returns a default-constructed element if the buffer is empty.
  [[nodiscard]] T peek() const;

  /// @brief Clears the buffer, resetting it to an empty state.
  void clear();

  /// @brief Checks whether the buffer is empty.
  /// @return `true` if the buffer is empty, `false` otherwise.
  [[nodiscard]] bool isEmpty() const;

  /// @brief Checks whether the buffer is full.
  /// @return `true` if the buffer is full, `false` otherwise.
  [[nodiscard]] bool isFull() const;

  /// @brief Retrieves the buffer's total capacity.
  /// @return The maximum number of elements the buffer can hold.
  [[nodiscard]] uint16_t getCapacity() const;

  /// @brief Retrieves the current number of elements stored in the buffer.
  /// @return The number of elements currently in the buffer.
  [[nodiscard]] uint16_t getSize() const;

  CircularBuffer(const CircularBuffer&) = delete;               // Define copy constructor.
  CircularBuffer& operator=(const CircularBuffer&) = delete;    // Define copy assignment operator.
  CircularBuffer(CircularBuffer&&) = delete;                    // Define move constructor.
  CircularBuffer& operator=(CircularBuffer&&) = delete;         // Define move assignment operator.

private:
  T buffer[buffSize];                         // Internal array to store elements.
  uint16_t head = 0U;                         // Index for writing new elements.
  uint16_t tail = 0U;                         // Index for reading the oldest elements.
  bool full = false;                          // Indicates whether the buffer is full.
};

template<class T, uint16_t buffSize>
CircularBuffer<T, buffSize>::CircularBuffer() : buffer() {}

template<class T, uint16_t buffSize>
void CircularBuffer<T, buffSize>::put(T item) {
  buffer[head] = item;
  head = (head + 1U) % buffSize;
  if(full) { tail = (tail + 1U) % buffSize; }
  full = (head == tail);
}

template<class T, uint16_t buffSize>
T CircularBuffer<T, buffSize>::pop() {
  T result = buffer[tail];
  tail = (tail + 1U) % buffSize;
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
  head = 0U;
  tail = 0U;
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