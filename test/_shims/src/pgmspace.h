#pragma once
#include <stdio.h>
#include <string.h>
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
#ifndef strcmp_P
#define strcmp_P strcmp
#endif
#ifndef strncmp_P
#define strncmp_P strncmp
#endif
#ifndef strncpy_P
#define strncpy_P strncpy
#endif
#ifndef memcpy_P
#define memcpy_P memcpy
#endif
