#pragma once
// Native-test shim: the port layer the CAN handler touches is the ISR yield, which has no
// meaning on a host with no scheduler.
#include "projdefs.h"

#define portYIELD_FROM_ISR(woken) ((void)(woken))
