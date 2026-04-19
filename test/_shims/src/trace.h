#pragma once
#include <iostream>
#include <stdlib.h>

// NOLINTBEGIN(bugprone-macro-parentheses)
#define LOG(x)                    \
  {                               \
    std::cout << x << std::flush; \
  }
#define TRACE(x)                    \
  {                                 \
    if (getenv("TRACE")) {          \
      std::cout << x << std::flush; \
    }                               \
  }
// NOLINTEND(bugprone-macro-parentheses)
