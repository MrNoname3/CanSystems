#pragma once
#include <iostream>

#include <stdlib.h>
// clang-format off
#define LOG(x) {std::cout << x << std::flush; }
#define TRACE(x) {if (getenv("TRACE")) { std::cout << x << std::flush; }}
// clang-format on
