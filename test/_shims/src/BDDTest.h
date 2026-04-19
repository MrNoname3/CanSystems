#pragma once

#include <cstdint>

void bddtest_suite(const char* name);
bool bddtest_test(const char*, uint32_t, const char*, bool);
void bddtest_start(const char*);
void bddtest_end();
int bddtest_summary();

inline void SUITE(const char* name) {
  bddtest_suite(name);
}
#define TEST(x)                                                                                           \
  {                                                                                                       \
    if (!bddtest_test(__FILE__, static_cast<uint32_t>(__LINE__), #x, static_cast<bool>(x))) return false; \
  }

inline void IT(const char* desc) {
  bddtest_start(desc);
}
#define END_IT     \
  {                \
    bddtest_end(); \
    return true;   \
  }

#define FINISH                \
  {                           \
    return bddtest_summary(); \
  }

#define IS_TRUE(x) TEST(x)
#define IS_FALSE(x) TEST(!(x))
#define IS_EQUAL(x, y) TEST(x == y)
#define IS_NOT_EQUAL(x, y) TEST(x != y)
