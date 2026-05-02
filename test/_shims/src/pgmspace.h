#pragma once
#include <stdio.h>
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PSTR
#define PSTR(x) (x)
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(x) (*(x))
#endif
#ifndef pgm_read_byte_near
#define pgm_read_byte_near(x) (*(x))
#endif
#ifndef pgm_read_word
#define pgm_read_word(x) (*(x))
#endif
#define snprintf_P snprintf
