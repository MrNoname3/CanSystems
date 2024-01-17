#ifndef CIRCULAR_BUFFER_HPP
#define CIRCULAR_BUFFER_HPP

#include <stdint.h>

template<class T, uint16_t buffSize> 
class CircularBuffer {
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
#endif // CIRCULAR_BUFFER_HPP
