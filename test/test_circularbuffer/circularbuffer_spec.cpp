#include "CircularBuffer.hpp"
#include "BDDTest.h"

bool test_empty_on_construction() {
  IT("is empty immediately after construction");
  CircularBuffer<uint8_t, 4> buf;
  IS_TRUE(buf.isEmpty());
  IS_FALSE(buf.isFull());
  IS_EQUAL(buf.getSize(), 0U);
  IS_EQUAL(buf.getCapacity(), 4U);
  END_IT
}

bool test_put_and_pop_single() {
  IT("put and pop single element preserves value");
  CircularBuffer<uint32_t, 4> buf;
  buf.put(42U);
  IS_FALSE(buf.isEmpty());
  IS_EQUAL(buf.getSize(), 1U);
  IS_EQUAL(buf.pop(), 42U);
  IS_TRUE(buf.isEmpty());
  END_IT
}

bool test_fifo_order() {
  IT("maintains FIFO ordering");
  CircularBuffer<uint8_t, 8> buf;
  buf.put(1U);
  buf.put(2U);
  buf.put(3U);
  IS_EQUAL(buf.pop(), 1U);
  IS_EQUAL(buf.pop(), 2U);
  IS_EQUAL(buf.pop(), 3U);
  END_IT
}

bool test_full_condition() {
  IT("reports full when capacity is reached");
  CircularBuffer<uint8_t, 3> buf;
  buf.put(1U);
  buf.put(2U);
  buf.put(3U);
  IS_TRUE(buf.isFull());
  IS_EQUAL(buf.getSize(), 3U);
  END_IT
}

bool test_overflow_overwrites_oldest() {
  IT("overwrites oldest element on overflow");
  CircularBuffer<uint8_t, 3> buf;
  buf.put(1U);
  buf.put(2U);
  buf.put(3U);
  buf.put(4U); // 1 is overwritten
  IS_EQUAL(buf.getSize(), 3U);
  IS_EQUAL(buf.pop(), 2U);
  IS_EQUAL(buf.pop(), 3U);
  IS_EQUAL(buf.pop(), 4U);
  END_IT
}

bool test_peek_does_not_remove() {
  IT("peek returns oldest element without removing it");
  CircularBuffer<uint8_t, 4> buf;
  buf.put(10U);
  buf.put(20U);
  IS_EQUAL(buf.peek(), 10U);
  IS_EQUAL(buf.getSize(), 2U);
  IS_EQUAL(buf.pop(), 10U);
  END_IT
}

bool test_peek_empty_returns_default() {
  IT("peek on empty buffer returns default-constructed value");
  CircularBuffer<uint32_t, 4> buf;
  IS_EQUAL(buf.peek(), 0U);
  END_IT
}

bool test_clear_resets_state() {
  IT("clear resets buffer to empty state");
  CircularBuffer<uint8_t, 4> buf;
  buf.put(1U);
  buf.put(2U);
  buf.put(3U);
  buf.clear();
  IS_TRUE(buf.isEmpty());
  IS_FALSE(buf.isFull());
  IS_EQUAL(buf.getSize(), 0U);
  END_IT
}

bool test_wrap_around() {
  IT("handles pointer wrap-around correctly");
  CircularBuffer<uint8_t, 4> buf;
  buf.put(1U);
  buf.put(2U);
  buf.put(3U);
  buf.put(4U);
  IS_EQUAL(buf.pop(), 1U);
  IS_EQUAL(buf.pop(), 2U);
  buf.put(5U);
  buf.put(6U); // wrap around
  IS_EQUAL(buf.pop(), 3U);
  IS_EQUAL(buf.pop(), 4U);
  IS_EQUAL(buf.pop(), 5U);
  IS_EQUAL(buf.pop(), 6U);
  IS_TRUE(buf.isEmpty());
  END_IT
}

bool test_multiple_overflows() {
  IT("handles multiple successive overflows correctly");
  CircularBuffer<uint8_t, 3> buf;
  for(uint8_t i = 1U; i <= 9U; i++) {
    buf.put(i);
  }
  IS_TRUE(buf.isFull());
  IS_EQUAL(buf.pop(), 7U);
  IS_EQUAL(buf.pop(), 8U);
  IS_EQUAL(buf.pop(), 9U);
  END_IT
}

struct Point {
  uint8_t x;
  uint8_t y;
};

bool test_struct_type() {
  IT("works with struct element types");
  CircularBuffer<Point, 4> buf;
  buf.put({ 1U, 2U });
  buf.put({ 3U, 4U });
  Point p = buf.pop();
  IS_EQUAL(p.x, 1U);
  IS_EQUAL(p.y, 2U);
  END_IT
}

bool test_pop_on_empty_returns_default_no_state_change() {
  IT("pop on empty buffer returns default value without corrupting state");
  CircularBuffer<uint8_t, 4> buf;
  IS_EQUAL(buf.pop(), 0U);
  IS_TRUE(buf.isEmpty());
  IS_EQUAL(buf.getSize(), 0U);
  buf.put(42U);
  IS_EQUAL(buf.pop(), 42U);
  END_IT
}

bool test_capacity_one() {
  IT("capacity-1 buffer overwrites on second put");
  CircularBuffer<uint8_t, 1> buf;
  buf.put(0xAAU);
  IS_TRUE(buf.isFull());
  buf.put(0xBBU);
  IS_TRUE(buf.isFull());
  IS_EQUAL(buf.getSize(), 1U);
  IS_EQUAL(buf.pop(), 0xBBU);
  IS_TRUE(buf.isEmpty());
  END_IT
}

bool test_capacity_two() {
  IT("capacity-2 buffer wraps around correctly");
  CircularBuffer<uint8_t, 2> buf;
  buf.put(1U);
  buf.put(2U);
  IS_TRUE(buf.isFull());
  buf.put(3U); // overwrites 1
  IS_EQUAL(buf.getSize(), 2U);
  IS_EQUAL(buf.pop(), 2U);
  IS_EQUAL(buf.pop(), 3U);
  IS_TRUE(buf.isEmpty());
  END_IT
}

bool test_peek_on_full_buffer() {
  IT("peek on a full buffer returns the oldest element without changing size");
  CircularBuffer<uint8_t, 3> buf;
  buf.put(10U);
  buf.put(20U);
  buf.put(30U);
  IS_TRUE(buf.isFull());
  IS_EQUAL(buf.peek(), 10U);
  IS_EQUAL(buf.getSize(), 3U);
  END_IT
}

bool test_pop_after_clear() {
  IT("pop after clear returns default and buffer stays operational");
  CircularBuffer<uint8_t, 4> buf;
  buf.put(1U);
  buf.put(2U);
  buf.clear();
  IS_EQUAL(buf.pop(), 0U);
  IS_TRUE(buf.isEmpty());
  IS_EQUAL(buf.getSize(), 0U);
  buf.put(42U);
  IS_EQUAL(buf.pop(), 42U);
  IS_TRUE(buf.isEmpty());
  END_IT
}

int main() {
  SUITE("CircularBuffer");
  test_empty_on_construction();
  test_put_and_pop_single();
  test_fifo_order();
  test_full_condition();
  test_overflow_overwrites_oldest();
  test_peek_does_not_remove();
  test_peek_empty_returns_default();
  test_clear_resets_state();
  test_wrap_around();
  test_multiple_overflows();
  test_struct_type();
  test_capacity_one();
  test_pop_on_empty_returns_default_no_state_change();
  test_capacity_two();
  test_peek_on_full_buffer();
  test_pop_after_clear();
  FINISH
}
