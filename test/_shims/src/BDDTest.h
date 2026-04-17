#ifndef bddtest_h
#define bddtest_h

#include <cstdint>

void bddtest_suite(const char* name);
bool bddtest_test(const char*, uint32_t, const char*, bool);
void bddtest_start(const char*);
void bddtest_end();
int bddtest_summary();

#define SUITE(x) { bddtest_suite(x); }
#define TEST(x) { if (!bddtest_test(__FILE__, static_cast<uint32_t>(__LINE__), #x, static_cast<bool>(x))) return false;  }

#define IT(x) { bddtest_start(x); }
#define END_IT { bddtest_end();return true;}

#define FINISH { return bddtest_summary(); }

#define IS_TRUE(x) TEST(x)
#define IS_FALSE(x) TEST(!(x))
#define IS_EQUAL(x,y) TEST(x==y)
#define IS_NOT_EQUAL(x,y) TEST(x!=y)

#endif
