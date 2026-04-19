#pragma once
#include <iostream>
#include <stdlib.h>

#define LOG(x) /* NOLINT(bugprone-macro-parentheses) */   \
  {                                                        \
    std::cout << x << std::flush;                          \
  }
#define TRACE(x) /* NOLINT(bugprone-macro-parentheses) */ \
  {                                                        \
    if (getenv("TRACE")) {                                 \
      std::cout << x << std::flush;                        \
    }                                                      \
  }
