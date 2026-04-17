#include "BDDTest.h"
#include "trace.h"
#include <cstdint>
#include <sstream>
#include <iostream>
#include <string>
#include <list>

uint16_t testCount = 0;
uint16_t testPasses = 0;
const char* testDescription = nullptr;

std::list<std::string> failureList;

void bddtest_suite(const char* name) {
    LOG(name << "\n");
}

bool bddtest_test(const char* file, uint32_t line, const char* assertion, bool result) {
    if (!result) {
        LOG("✗\n");
        std::ostringstream os;
        os << "   ! "<<testDescription<<"\n      " <<file << ":" <<line<<" : "<<assertion<<" ["<<static_cast<int>(result)<<"]";
        failureList.push_back(os.str());
    }
    return result;
}

void bddtest_start(const char* description) {
    LOG(" - "<<description<<" ");
    testDescription = description;
    testCount++;
}
void bddtest_end() {
    LOG("✓\n");
    testPasses++;
}

int bddtest_summary() {
    for (const std::string& failure : failureList) {
        LOG("\n");
        LOG(failure);
        LOG("\n");
    }

    LOG(std::dec << testPasses << "/" << testCount << " tests passed\n\n");
    if (testPasses == testCount) {
        return 0;
    }
    return 1;
}
