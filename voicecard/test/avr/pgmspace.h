// Host shim so voicecard DSP headers compile and run on a PC.
#ifndef TEST_AVR_PGMSPACE_H_
#define TEST_AVR_PGMSPACE_H_
#include <stdint.h>
#include <string.h>
#define PROGMEM
#define E2END 0x3FF  // ATmega328P
#define PSTR(s) (s)
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define memcpy_P memcpy
#define strncpy_P strncpy
typedef char prog_char;
typedef uint8_t prog_uint8_t;
typedef int8_t prog_int8_t;
typedef uint16_t prog_uint16_t;
typedef int16_t prog_int16_t;
typedef uint32_t prog_uint32_t;
#endif
