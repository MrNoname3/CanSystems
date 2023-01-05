#ifndef __CIRCULAR_BUFFER_HPP__
#define __CIRCULAR_BUFFER_HPP__

template<class T, uint16_t buffSize> 
class CircularBuffer {

public:

  /// Circular buffer class constructor.
  ///
  /// @brief This class is a multitype static size circular buffer.
  /// @param T Selected datatype.
  /// @param buffSize Selected buffer size.
  CircularBuffer();

  virtual ~CircularBuffer();

  /// @brief Puts an element to the data buffer.
  /// @param item Element to store.
  void put(const T item);

  /// @brief Gets an element from the buffer.
  /// @return Returns with the element.
  T pop(void);

  /// @brief Checks if the buffer empty is.
  /// @return Returns true if the buffer is full.
  bool isEmpty(void) const;

  /// @brief Checks buffer free capacity.
  /// @return Returns with the free capacity.
  uint16_t getCapacity(void) const;

  /// @brief Checks buffer size.
  /// @return Returns with the size.
  uint16_t getSize(void) const;

private:
  T buffer[buffSize];                         // Data buffer.
  uint16_t head = 0;                          // Circular buffer write index.
  uint16_t tail = 0;                          // Circular buffer read index.
  bool isFull = false;                        // Stores if the buffer full is.

};

template<class T, uint16_t buffSize>
CircularBuffer<T, buffSize>::CircularBuffer() {

}

template<class T, uint16_t buffSize>
CircularBuffer<T, buffSize>::~CircularBuffer(){

}

template<class T, uint16_t buffSize>
void CircularBuffer<T, buffSize>::put(const T item) {
  buffer[head] = item;
  head = (head + 1) % buffSize;
  if(isFull == true) {
    tail = (tail + 1) % buffSize;
  }
  isFull = (head == tail);
}

template<class T, uint16_t buffSize>
T CircularBuffer<T, buffSize>::pop(void) {
  auto result = buffer[tail];
  tail = (tail + 1) % buffSize;
  isFull = false;
  return result;
}

template<class T, uint16_t buffSize>
bool CircularBuffer<T, buffSize>::isEmpty(void) const {
  return tail == head;
}

template<class T, uint16_t buffSize>
uint16_t CircularBuffer<T, buffSize>::getCapacity(void) const {
  return buffSize;
}

template<class T, uint16_t buffSize>
uint16_t CircularBuffer<T, buffSize>::getSize(void) const {
  if(isFull == true) {
    return buffSize;
  }
  if (head >= tail) {
    return head - tail;
  }
  return buffSize + head - tail;
}

#endif