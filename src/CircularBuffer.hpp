#ifndef CIRCULAR_BUFFER_HPP
#define CIRCULAR_BUFFER_HPP

template<class T, uint16_t buffSize> 
class CircularBuffer {

public:

  /// @brief Constructor of a multitype static size circular buffer.
  /// @param T Selected datatype.
  /// @param buffSize Selected buffer size.
  CircularBuffer() = default;

  /// @brief Destructor of the object.
  virtual ~CircularBuffer() = default;

  /// @brief Puts an element to the data buffer.
  /// @param item Element to store.
  void put(T item);

  /// @brief Gets an element from the buffer.
  /// @return Returns with the element.
  T pop();

  /// @brief Checks if the buffer empty is.
  /// @return Returns true if the buffer is full.
  bool isEmpty() const;

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
  uint16_t head = 0;                          // Circular buffer write index.
  uint16_t tail = 0;                          // Circular buffer read index.
  bool isFull = false;                        // Stores if the buffer full is.

};

template<class T, uint16_t buffSize>
void CircularBuffer<T, buffSize>::put(T item) {
  buffer[head] = item;
  head = (head + 1) % buffSize;
  if(isFull == true) {
    tail = (tail + 1) % buffSize;
  }
  isFull = (head == tail);
}

template<class T, uint16_t buffSize>
T CircularBuffer<T, buffSize>::pop() {
  auto result = buffer[tail];
  tail = (tail + 1) % buffSize;
  isFull = false;
  return result;
}

template<class T, uint16_t buffSize>
bool CircularBuffer<T, buffSize>::isEmpty() const {
  return tail == head;
}

template<class T, uint16_t buffSize>
uint16_t CircularBuffer<T, buffSize>::getCapacity() const {
  return buffSize;
}

template<class T, uint16_t buffSize>
uint16_t CircularBuffer<T, buffSize>::getSize() const {
  if(isFull == true) {
    return buffSize;
  }
  if (head >= tail) {
    return head - tail;
  }
  return buffSize + head - tail;
}

#endif // CIRCULAR_BUFFER_HPP